#include <graphics/uploader.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <utils/thread.h>

namespace chord::graphics
{
	constexpr uint32 kAsyncStaticUploaderNum  = 2;
	constexpr uint32 kAsyncDynamicUploaderNum = 1;

	//
	constexpr uint32 kUploadMemoryAlign = 16; // For BC.

	static inline std::string getTransferBufferUniqueId()
	{
		static std::atomic<size_t> counter = 0;
		return "AsyncUploadBuffer_" + std::to_string(counter.fetch_add(1));
	}

	IAsyncUploader::IAsyncUploader(const std::string& name, AsyncUploaderManager& in)
		: m_name(name), m_manager(in)
	{
		m_future = std::async(std::launch::async, [this]()
		{
			// Create fence of this uploader state.
			m_fence = helper::createFence(0);

			// Create async pool.
			m_poolAsync = helper::createCommandPool(m_manager.getQueueFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
			m_commandBufferAsync = helper::allocateCommandBuffer(m_poolAsync);

			LOG_TRACE("Async uploader {0} create.", m_name);

			while (m_bRun.load())
			{
				threadFunction();
			}

			// Before release you must ensure all work finish.
			check(!working());

			// Release command pool.
			helper::destroyCommandPool(m_poolAsync);

			// Fence release.
			helper::destroyFence(m_fence);

			LOG_TRACE("Async uploader {0} destroy.", m_name);
		});
	}

	void IAsyncUploader::startRecordAsync()
	{
		helper::resetCommandBuffer(m_commandBufferAsync);
		helper::beginCommandBuffer(m_commandBufferAsync, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	}

	void IAsyncUploader::endRecordAsync()
	{
		helper::endCommandBuffer(m_commandBufferAsync);
	}

	void DynamicAsyncUploader::threadFunction()
	{
		auto tickLogic = [&]()
		{
			check(!m_processingTask);
			check(m_bWorking == false);

			// Get task from manager.
			m_manager.dynamicTasksAction([&, this](std::queue<AsyncUploadTaskRef>& srcQueue)
			{
				if (srcQueue.size() == 0)
				{
					return;
				}

				if (srcQueue.size() > 0)
				{
					m_processingTask = srcQueue.front();
					srcQueue.pop();
				}
			});

			// May stole by other thread, release stage buffer and return.
			if (!m_processingTask)
			{
				m_stageBuffer = nullptr;
				return;
			}

			// Now start working.
			m_bWorking = true;

			const auto requireSize = m_processingTask->size;
			check(requireSize > m_manager.getDynamicUploadMinSize());

			const bool bShouldRecreate =
				   (m_stageBuffer == nullptr)                          // No create yet.
				|| (m_stageBuffer->get().getSize() < 1 * requireSize)  // Size no enough.
				|| (m_stageBuffer->get().getSize() > 4 * requireSize); // Size too big, waste too much.
			if (bShouldRecreate)
			{
				// Use buffer pool.
				m_stageBuffer = getContext().getBufferPool().createHostVisibleCopyUpload(getTransferBufferUniqueId(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, SizedBuffer(requireSize, nullptr));
			}

			startRecordAsync();
			m_stageBuffer->get().map();
			m_stageBuffer->get().invalidate();
			m_processingTask->task(0, m_manager.getQueueFamily(), m_stageBuffer->get().getMapped(), m_commandBufferAsync, m_stageBuffer->get());
			m_stageBuffer->get().flush();

			m_stageBuffer->get().unmap();
			endRecordAsync();

			m_manager.pushSubmitFunctions(this);
		};

		if (!m_manager.dynamicLoadAssetTaskEmpty() && !working())
		{
			tickLogic();
		}
		else
		{
			std::unique_lock<std::mutex> lock(m_manager.getDynamicMutex());
			m_manager.getDynamicCondition().wait(lock);
		}
	}

	void StaticAsyncUploader::threadFunction()
	{
		auto tickLogic = [&]()
		{
			check(m_processingTasks.empty());
			check(m_bWorking == false);

			// Get static task from manager.
			m_manager.staticTasksAction([&, this](std::queue<AsyncUploadTaskRef>& srcQueue)
			{
				// Empty already.
				if (srcQueue.size() == 0)
				{
					return;
				}

				uint32 availableSize = m_manager.getStaticUploadMaxSize();
				while (srcQueue.size() > 0)
				{
					AsyncUploadTaskRef processTask = srcQueue.front();

					uint32 requireSize = processTask->size;
					check(requireSize < m_manager.getDynamicUploadMinSize());

					// Small buffer use static uploader.
					if (availableSize > requireSize)
					{
						m_processingTasks.push_back(processTask);
						availableSize -= requireSize;
						srcQueue.pop();
					}
					else
					{
						// No enough space for new task, break task push.
						break;
					}
				}
			});

			// May stole by other thread, no processing task, return.
			if (m_processingTasks.size() <= 0)
			{
				return;
			}

			// Now can work.
			m_bWorking = true;

			if (!m_stageBuffer)
			{
				const auto baseBufferSize = static_cast<VkDeviceSize>(m_manager.getStaticUploadMaxSize());
				m_stageBuffer = getContext().createStageUploadBuffer(getTransferBufferUniqueId(), SizedBuffer(baseBufferSize, nullptr));
			}

			// Do copy action here.
			startRecordAsync();

			m_stageBuffer->map();
			{
				m_stageBuffer->invalidate();
				uint32 totalSize = 0;
				uint8* mapped = (uint8*)m_stageBuffer->getMapped();
				for (auto i = 0; i < m_processingTasks.size(); i++)
				{
					const auto taskSize = ((m_processingTasks[i]->size + kUploadMemoryAlign - 1) / kUploadMemoryAlign) * kUploadMemoryAlign;
					m_processingTasks[i]->task(totalSize, m_manager.getQueueFamily(), mapped, m_commandBufferAsync, *m_stageBuffer);

					totalSize += taskSize;
					mapped = mapped + taskSize;
				}
				m_stageBuffer->flush();
			}
			m_stageBuffer->unmap();

			endRecordAsync();
			m_manager.pushSubmitFunctions(this);
		};

		if (!m_manager.staticLoadAssetTaskEmpty() && !working())
		{
			tickLogic();
		}
		else
		{
			std::unique_lock<std::mutex> lock(m_manager.getStaticMutex());
			m_manager.getStaticCondition().wait(lock);
		}
	}

	AsyncUploaderManager::AsyncUploaderManager(uint32 staticUploaderMaxSize, uint32 dynamicUploaderMinSize)
		: m_dynamicUploaderMinSize(dynamicUploaderMinSize * 1024 * 1024)
		, m_staticUploaderMaxSize(staticUploaderMaxSize * 1024 * 1024)
		, m_queueFamily(getContext().getQueuesInfo().copyFamily.get())
	{
		for (size_t i = 0; i < kAsyncStaticUploaderNum; i++)
		{
			std::string name = "StaticAsyncUpload_" + std::to_string(i);
			m_staticUploaders.push_back(
				std::make_unique<StaticAsyncUploader>(name, *this));
		}

		for (size_t i = 0; i < kAsyncDynamicUploaderNum; i++)
		{
			std::string name = "DynamicAsyncUpload_" + std::to_string(i);
			m_dynamicUploaders.push_back(
				std::make_unique<DynamicAsyncUploader>(name, *this));
		}
	}

	void AsyncUploaderManager::addTask(size_t requireSize, AsyncUploadTaskFunc&& func, AsyncUploadFinishFunc&& finishCallback)
	{
		auto taskRef = std::make_shared<AsyncUploadTask>();
		taskRef->size = requireSize;
		taskRef->task = std::move(func);
		taskRef->finishCallback = std::move(finishCallback);

		if (requireSize >= getDynamicUploadMinSize())
		{
			dynamicTasksAction([&](auto& queue){ queue.push(taskRef); });
		}
		else
		{
			staticTasksAction([&](auto& queue){ queue.push(taskRef); });
		}
	}

	bool AsyncUploaderManager::busy() const
	{
		if (!staticLoadAssetTaskEmpty())
		{
			return true;
		}
		if (!dynamicLoadAssetTaskEmpty())
		{
			return true;
		}

		for (size_t i = 0; i < m_staticUploaders.size(); i++)
		{
			if (m_staticUploaders[i]->working())
			{
				return true;
			}
		}
		for (size_t i = 0; i < m_dynamicUploaders.size(); i++)
		{
			if (m_dynamicUploaders[i]->working())
			{
				return true;
			}
		}

		return false;
	}

	void AsyncUploaderManager::submitObjects()
	{
		std::lock_guard<std::mutex> lock(m_submitObjectsMutex);
		if (!m_submitObjects.empty())
		{
			size_t indexQueue = 0;

			size_t maxIndex = getContext().getQueuesInfo().copyQueues.size();
			check(maxIndex > 0);

			for (auto* obj : m_submitObjects)
			{
				VkSubmitInfo submitInfo{};
				submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				submitInfo.commandBufferCount = 1;
				submitInfo.pCommandBuffers = &obj->getCommandBuffer();

				checkVkResult(vkQueueSubmit(getContext().getQueuesInfo().copyQueues.at(indexQueue).queue, 1, &submitInfo, obj->getFence()));

				indexQueue ++;
				if (indexQueue == maxIndex)
				{
					indexQueue = 0;
				}

				m_pendingObjects.push_back(obj);
			}
			m_submitObjects.clear();
		}
	}

	void AsyncUploaderManager::syncPendingObjects()
	{
		std::lock_guard<std::mutex> lock(m_submitObjectsMutex);
		std::erase_if(m_pendingObjects, [](auto* obj)
		{
			bool bResult = false;
			if (vkGetFenceStatus(getDevice(), obj->getFence()) == VK_SUCCESS)
			{
				obj->onFinished();
				bResult = true;
			}
			return bResult;
		});
	}

	void AsyncUploaderManager::pushSubmitFunctions(IAsyncUploader* f)
	{
		std::lock_guard<std::mutex> lock(m_submitObjectsMutex);
		m_submitObjects.push_back(f);
	}

	void AsyncUploaderManager::tick(const ApplicationTickData& tickData)
	{
		// Flush submit functions.
		syncPendingObjects();
		submitObjects();

		if (!staticLoadAssetTaskEmpty())
		{
			getStaticCondition().notify_one();
		}
		if (!dynamicLoadAssetTaskEmpty())
		{
			getDynamicCondition().notify_one();
		}
	}

	void AsyncUploaderManager::flushTask()
	{
		while (busy())
		{
			getStaticCondition().notify_all();
			getDynamicCondition().notify_all();

			submitObjects();
			syncPendingObjects();
		}
	}

	AsyncUploaderManager::~AsyncUploaderManager()
	{
		flushTask();
		LOG_INFO("Start release async uploader threads...");

		for (size_t i = 0; i < m_staticUploaders.size(); i++)
		{
			m_staticUploaders[i]->stop();
		}
		for (size_t i = 0; i < m_dynamicUploaders.size(); i++)
		{
			m_dynamicUploaders[i]->stop();
		}
		getStaticCondition().notify_all();
		getDynamicCondition().notify_all();

		// Wait all futures.
		for (size_t i = 0; i < m_staticUploaders.size(); i++)
		{
			m_staticUploaders[i]->wait();
			m_staticUploaders[i].reset();
		}
		for (size_t i = 0; i < m_dynamicUploaders.size(); i++)
		{
			m_dynamicUploaders[i]->wait();
			m_dynamicUploaders[i].reset();
		}
		LOG_INFO("All async uploader threads release.");
	}

	void IAsyncUploader::onFinished()
	{
		check(m_bWorking);

		m_bWorking = false;
		checkVkResult(vkResetFences(getDevice(), 1, &m_fence));
	}

	void DynamicAsyncUploader::onFinished()
	{
		checkMsgf(m_processingTask, "Awake dynamic async loader but no task feed, fix me!");

		if (m_processingTask->finishCallback)
		{
			m_processingTask->finishCallback();
		}
		m_processingTask = nullptr;

		IAsyncUploader::onFinished();
	}

	void StaticAsyncUploader::onFinished()
	{
		check(!m_processingTasks.empty());

		for (auto& uploadingTask : m_processingTasks)
		{
			if (uploadingTask->finishCallback)
			{
				uploadingTask->finishCallback();
			}
		}
		m_processingTasks.clear();

		IAsyncUploader::onFinished();
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



