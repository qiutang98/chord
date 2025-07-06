#include <graphics/uploader.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <utils/thread.h>
#include <application/application.h>

namespace chord::graphics
{
	//
	constexpr uint32 kUploadMemoryAlign = 16; // For BC.

	static inline std::string getTransferBufferUniqueId()
	{
		static std::atomic<size_t> counter = 0;
		return "AsyncUploadBuffer_" + std::to_string(counter.fetch_add(1));
	}

	AsyncUploaderBase::AsyncUploaderBase(const std::string& name, AsyncUploaderManager& manager)
		: m_name(name), m_manager(manager)
	{
		// Create async pool.
		m_poolAsync = helper::createCommandPool(manager.getQueueFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		m_commandBufferAsync = helper::allocateCommandBuffer(m_poolAsync);
		m_fence = helper::createFence(VK_FENCE_CREATE_SIGNALED_BIT);
	}

	AsyncUploaderBase::~AsyncUploaderBase()
	{
		// Wait all processing task finish.
		jobsystem::busyWaitUntil([this]() { return m_processingTasks.empty(); }, EBusyWaitType::All);

		// Release command pool.
		helper::destroyCommandPool(m_poolAsync);
		helper::destroyFence(m_fence);
	}

	void AsyncUploaderBase::startRecordAsync() const
	{
		helper::resetCommandBuffer(m_commandBufferAsync);
		helper::beginCommandBuffer(m_commandBufferAsync, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	}

	void AsyncUploaderBase::endRecordAsync() const
	{
		helper::endCommandBuffer(m_commandBufferAsync);
	}

	bool AsyncUploaderBase::gpuTaskFinish() const
	{
		if (Application::get().getRuntimePeriod() >= ERuntimePeriod::BeforeReleasing)
		{
			return true;
		}
		return vkGetFenceStatus(getDevice(), m_fence) == VK_SUCCESS;
	}

	bool AsyncUploaderBase::allTaskFinish() const
	{
		bool bFinish = gpuTaskFinish() && m_processingTasks.empty();
		if (m_dispatchJob)
		{
			bFinish &= m_dispatchJob->bFinish;
		}

		return bFinish;
	}

	// Only execute on main thread.
	void AsyncUploaderBase::triggleSubmitJob(bool bForceDispatch)
	{
		check(isInMainThread());
		bool bDispatch = bForceDispatch ? true : (m_dispatchJob == nullptr || m_dispatchJob->bFinish);

		if (!bDispatch)
		{
			return;
		}

		m_dispatchJob = jobsystem::launch("AsyncUpload", EJobFlags::None, [this]()
		{
			ZoneScopedN("AsyncUpload");

			// Wait until all prev task job done.
			jobsystem::busyWaitUntil([this]() { return gpuTaskFinish(); }, EBusyWaitType::All);

			// Clean some finished job.
			while (m_processingTasks.size() > 0)
			{
				auto task = m_processingTasks.front().task;

				static std::atomic<int32> sDebugTaskCount { 0 };
				LOG_TRACE("Debug task count add {}.", sDebugTaskCount.fetch_add(1));
				ENQUEUE_MAIN_COMMAND([task]() { LOG_TRACE("Debug task count sub {}.", sDebugTaskCount.fetch_sub(1)); task->finishCallback(); });
				
				m_processingTasks.pop();
			}

			// Now GPU task finish, can start new task.
			if (doTask())
			{
				checkVkResult(vkResetFences(getDevice(), 1, &m_fence));

				const auto& copyQueues = getContext().getQueuesInfo().copyQueues;

				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &m_commandBufferAsync;
				checkVkResult(vkQueueSubmit(copyQueues.at(0).queue, 1, &submitInfo, m_fence));
			}

			// 
			if (!m_pendingTasks.isEmpty() || m_processingTasks.size() > 0)
			{
				ENQUEUE_MAIN_COMMAND([this]() { triggleSubmitJob(true); });
			}
		}, { m_dispatchJob });
	}

	void AsyncUploaderBase::addTask(AsyncUploadTaskRef task)
	{
		m_pendingTasks.enqueue(std::move(task));
		ENQUEUE_MAIN_COMMAND([this]() { triggleSubmitJob(); });
	}

	void AsyncUploaderBase::pushTaskToProcessingQueue(AsyncUploadTaskRef task, GPUBufferRef buffer)
	{
		// Add to pending queue.
		ExecutingTask executingTask;
		executingTask.task = task;
		executingTask.buffer = buffer;
		m_processingTasks.push(executingTask);
	}

	bool StaticAsyncUploader::doTask()
	{
		ZoneScoped;

		bool bNeedSubmit = false;
		if (!m_stageBuffer)
		{
			const auto baseBufferSize = static_cast<VkDeviceSize>(m_manager.getStaticUploadMaxSize());
			m_stageBuffer = getContext().createStageUploadBuffer(getTransferBufferUniqueId(), SizedBuffer(baseBufferSize, nullptr));
		}

		startRecordAsync();
		m_stageBuffer->map();
		m_stageBuffer->invalidate();
		{
			uint32 totalSize = 0;
			uint8* mapped = (uint8*)m_stageBuffer->getMapped();
			while (true)
			{
				AsyncUploadTaskRef pendingTask = nullptr;
				if (m_pendingTasks.getDequeue(pendingTask))
				{
					const auto taskSize = alignRoundingUp(pendingTask->size, kUploadMemoryAlign);
					if (totalSize + taskSize < m_manager.getStaticUploadMaxSize())
					{
						check(m_pendingTasks.dequeue(pendingTask));

						// Executing task.
						pendingTask->task(totalSize, m_manager.getQueueFamily(), mapped, m_commandBufferAsync, *m_stageBuffer);
						bNeedSubmit = true;

						pushTaskToProcessingQueue(pendingTask, m_stageBuffer);

						// Step next task.
						totalSize += taskSize;
						mapped = mapped + taskSize;
						continue;
					}
				}
				break;
			}
		}
		m_stageBuffer->flush();
		m_stageBuffer->unmap();
		endRecordAsync();

		return bNeedSubmit;
	}

	bool DynamicAsyncUploader::doTask()
	{
		ZoneScoped;

		bool bNeedSubmit = false;

		AsyncUploadTaskRef pendingTask = nullptr;
		if (m_pendingTasks.dequeue(pendingTask))
		{
			const auto requireSize = alignRoundingUp(pendingTask->size, kUploadMemoryAlign);
			check(requireSize > m_manager.getDynamicUploadMinSize());

			const bool bShouldRecreate =
				   (m_stageBuffer == nullptr)                          // No create yet.
				|| (m_stageBuffer->get().getSize() < 1 * requireSize)  // Size no enough.
				|| (m_stageBuffer->get().getSize() > 4 * requireSize); // Size too big, waste too much.

			if (bShouldRecreate)
			{
				m_stageBuffer = getContext().getBufferPool().createHostVisibleCopyUpload(getTransferBufferUniqueId(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, SizedBuffer(requireSize, nullptr));
			}

			startRecordAsync();
			m_stageBuffer->get().map();
			m_stageBuffer->get().invalidate();
			{
				pendingTask->task(0, m_manager.getQueueFamily(), m_stageBuffer->get().getMapped(), m_commandBufferAsync, m_stageBuffer->get());
				pushTaskToProcessingQueue(pendingTask, m_stageBuffer->getGPUBufferRef());
			}
			m_stageBuffer->get().flush();
			m_stageBuffer->get().unmap();
			endRecordAsync();

			bNeedSubmit = true;
		}

		return bNeedSubmit;
	}

	AsyncUploaderManager::AsyncUploaderManager(uint32 staticUploaderMaxSize, uint32 dynamicUploaderMinSize)
		: m_dynamicUploaderMinSize(dynamicUploaderMinSize * 1024 * 1024)
		, m_staticUploaderMaxSize(staticUploaderMaxSize * 1024 * 1024)
		, m_queueFamily(getContext().getQueuesInfo().copyFamily.get())
	{
		m_staticUploader = std::make_unique<StaticAsyncUploader>("StaticAsyncUpload", *this);
		m_dynamicUploader = std::make_unique<DynamicAsyncUploader>("DynamicAsyncUpload", *this);
	}

	void AsyncUploaderManager::addTask(size_t requireSize, AsyncUploadTaskFunc&& func, AsyncUploadFinishFunc&& finishCallback)
	{
		auto taskRef = std::make_shared<AsyncUploadTask>();
		taskRef->size = requireSize;
		taskRef->task = std::move(func);
		taskRef->finishCallback = std::move(finishCallback);

		if (requireSize >= getDynamicUploadMinSize())
		{
			m_dynamicUploader->addTask(taskRef);
		}
		else
		{
			m_staticUploader->addTask(taskRef);
		}
	}

	void AsyncUploaderManager::flushTask()
	{
		check(isInMainThread());

		// 
		m_staticUploader->triggleSubmitJob();
		m_dynamicUploader->triggleSubmitJob();

		//
		jobsystem::busyWaitUntil([this]() { return !this->busy(); }, EBusyWaitType::All);
	}

	AsyncUploaderManager::~AsyncUploaderManager()
	{
		flushTask();
	}

	GPUTextureAsset::GPUTextureAsset(
		GPUTextureAsset* fallback,
		const std::string& name,
		const VkImageCreateInfo& createInfo,
		const VmaAllocationCreateInfo& vmaCreateInfo)
		: IUploadAsset(fallback)
	{
		m_texture = std::make_shared<GPUTexture>(name, createInfo, vmaCreateInfo);
	}

	GPUTextureAsset::~GPUTextureAsset()
	{
	}

	void GPUTextureAsset::prepareToUpload(VkCommandBuffer cmd, uint32 destFamily, VkImageSubresourceRange range)
	{
		GPUTextureSyncBarrierMasks masks{};

		masks.barrierMasks.queueFamilyIndex = destFamily;

		masks.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		masks.barrierMasks.accesMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		m_texture->transition(cmd, masks, range);
	}

	void GPUTextureAsset::finishUpload(VkCommandBuffer cmd, uint32 destFamily, VkImageSubresourceRange range)
	{
		GPUTextureSyncBarrierMasks masks{};

		masks.barrierMasks.queueFamilyIndex = destFamily;

		masks.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		masks.barrierMasks.accesMask = VK_ACCESS_SHADER_READ_BIT;

		m_texture->transition(cmd, masks, range);
	}

	uint32 GPUTextureAsset::getSRV(const VkImageSubresourceRange& range, VkImageViewType viewType)
	{
		return getReadyImage()->requireView(range, viewType, true, false).SRV.get();
	}
}