#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/command_list.h>


namespace chord::graphics
{

	CommandBuffer::~CommandBuffer()
	{
		// Release pool, self management memory.
		helper::destroyCommandPool(commandPool);
	}

	Queue::Queue(const std::string& name, const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family)
		: m_name(name)
		, m_swapchain(swapchain)
		, m_queue(queue)
		, m_queueType(type)
		, m_queueFamily(family)
	{
		m_timeline.waitValue = 0U;
		m_timeline.waitFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		const std::string kSemaphoreName = std::format("{} TimelineSemaphore", name);
		m_timeline.semaphore = helper::createTimelineSemaphore(kSemaphoreName, m_timeline.waitValue);
	}

	Queue::~Queue()
	{
		helper::destroySemaphore(m_timeline.semaphore);
	}

	void Queue::checkRecording() const
	{
		check(m_activeCmdCtx.command != nullptr);
	}

	void Queue::copyBuffer(PoolBufferRef src, PoolBufferRef dest, size_t size, size_t srcOffset, size_t destOffset)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(src);
		cmd->addReferenceResource(dest);

		{
			GPUSyncBarrierMasks srcMask  = { .queueFamilyIndex = m_queueFamily, .accesMask = VK_ACCESS_TRANSFER_READ_BIT };
			GPUSyncBarrierMasks destMask = { .queueFamilyIndex = m_queueFamily, .accesMask = VK_ACCESS_TRANSFER_WRITE_BIT };

			VkBufferMemoryBarrier2 barriers[2];

			barriers[0] = src->get().updateBarrier(srcMask);
			barriers[1] = dest->get().updateBarrier(destMask);

			helper::pipelineBarrier(cmd->commandBuffer, 0, countof(barriers), barriers, 0, nullptr);
		}

		VkBufferCopy2 copyRegion{};
		copyRegion.sType     = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
		copyRegion.srcOffset = srcOffset;
		copyRegion.dstOffset = destOffset;
		copyRegion.size      = size;

		VkCopyBufferInfo2 copyInfo { };
		copyInfo.sType       = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
		copyInfo.srcBuffer   = src->get();
		copyInfo.dstBuffer   = dest->get();
		copyInfo.regionCount = 1;
		copyInfo.pRegions    = &copyRegion;

		vkCmdCopyBuffer2(cmd->commandBuffer, &copyInfo);
	}

	CommandBufferRef Queue::getOrCreateCommandBuffer()
	{
		CommandBufferRef cmdBuf;
		if (m_commandsPool.empty())
		{
			cmdBuf = std::make_shared<CommandBuffer>();

			cmdBuf->commandPool = helper::createCommandPool(m_queueFamily,
				VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

			cmdBuf->commandBuffer = helper::allocateCommandBuffer(cmdBuf->commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		}
		else
		{
			cmdBuf = m_commandsPool.front();
			m_commandsPool.pop_front();
		}

		return cmdBuf;
	}

	void Queue::beginCommand(const std::vector<QueueTimeline>& waitValue)
	{
		check(m_activeCmdCtx.command == nullptr);

		m_activeCmdCtx.command = getOrCreateCommandBuffer();
		check(m_activeCmdCtx.command->isReferenceResourcesEmpty());
		m_activeCmdCtx.waitValue = waitValue;

		helper::beginCommandBuffer(m_activeCmdCtx.command->commandBuffer);
	}

	QueueTimeline Queue::endCommand(VkPipelineStageFlags waitFlags)
	{
		check(m_activeCmdCtx.command != nullptr);

		helper::endCommandBuffer(m_activeCmdCtx.command->commandBuffer);

		// Signal value increment.
		stepTimeline(waitFlags);
		m_activeCmdCtx.command->signalValue = m_timeline.waitValue;

		std::vector<uint64> waitValues{};
		std::vector<VkSemaphore> waitSemaphores{};
		std::vector<VkPipelineStageFlags> kWaitFlags{};
		for (auto& wait : m_activeCmdCtx.waitValue)
		{
			waitValues.push_back(wait.waitValue);
			waitSemaphores.push_back(wait.semaphore);
			kWaitFlags.push_back(wait.waitFlags);
		}

		VkTimelineSemaphoreSubmitInfo timelineInfo;
		timelineInfo.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
		timelineInfo.pNext                     = NULL;
		timelineInfo.waitSemaphoreValueCount   = waitValues.size();
		timelineInfo.pWaitSemaphoreValues      = waitValues.data();
		timelineInfo.signalSemaphoreValueCount = 1;
		timelineInfo.pSignalSemaphoreValues    = &m_activeCmdCtx.command->signalValue;

		VkSubmitInfo submitInfo;
		submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pNext                = &timelineInfo;
		submitInfo.waitSemaphoreCount   = waitSemaphores.size();
		submitInfo.pWaitSemaphores      = waitSemaphores.data();
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores    = &m_timeline.semaphore;
		submitInfo.commandBufferCount   = 1;
		submitInfo.pCommandBuffers      = &m_activeCmdCtx.command->commandBuffer;
		submitInfo.pWaitDstStageMask    = kWaitFlags.data();

		//
		vkQueueSubmit(m_queue, 1, &submitInfo, NULL);

		m_usingCommands.push_back(m_activeCmdCtx.command);
		m_activeCmdCtx = {};

		return m_timeline;
	}

	void Queue::sync(uint32 freeFrameCount)
	{
		constexpr size_t kCommandCountCheckPoint = 1000;
		if (m_usingCommands.size() > kCommandCountCheckPoint)
		{
			LOG_ERROR("Current exist {} command in queue, generally no need so much, maybe exist some leak.", m_usingCommands.size());
		}
		
		// Free command when finish.
		if (!m_usingCommands.empty())
		{
			uint64 currentValue;
			vkGetSemaphoreCounterValue(getDevice(), m_timeline.semaphore, &currentValue);

			auto usingCmd = m_usingCommands.begin();

			while (usingCmd != m_usingCommands.end())
			{
				uint64 usingTimelineValue = (*usingCmd)->signalValue;

				if ((usingTimelineValue + freeFrameCount) <= currentValue)
				{
					// Empty unused resources.
					(*usingCmd)->clearReferenceResources();

					m_commandsPool.push_back(*usingCmd);
					usingCmd = m_usingCommands.erase(usingCmd);
				}
				else
				{
					break;
				}
			}
		}
	}

	CommandList::CommandList(Swapchain& swapchain)
		: m_swapchain(swapchain)
	{
		const auto& queueInfos = getContext().getQueuesInfo();
		m_graphicsQueue = std::make_unique<GraphicsQueue>("Graphics Queue", m_swapchain, EQueueType::Graphics, queueInfos.graphcisQueues[0].queue, queueInfos.graphicsFamily.get());
		m_asyncComputeQueue = std::make_unique<GraphicsOrComputeQueue>("Async Compute Queue", m_swapchain, EQueueType::Compute, queueInfos.computeQueues[0].queue, queueInfos.computeFamily.get());
		m_asyncCopyQueue = std::make_unique<Queue>("Async Copy Queue", m_swapchain, EQueueType::Copy, queueInfos.copyQueues[0].queue, queueInfos.copyFamily.get());
	}

	CommandList::~CommandList()
	{

	}

	void CommandList::addReferenceResource(ResourceRef resource)
	{
		m_swapchain.addReferenceResource(resource);
	}

	void CommandList::sync(uint32 freeFrameCount)
	{
		m_graphicsQueue->sync(freeFrameCount);
		m_asyncComputeQueue->sync(freeFrameCount);
		m_asyncCopyQueue->sync(freeFrameCount);
	}

	GraphicsQueue::GraphicsQueue(const std::string& name, const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family)
		: GraphicsOrComputeQueue(name, swapchain, type, queue, family)
	{
	}

	void GraphicsOrComputeQueue::clearImage(PoolTextureRef image, const VkClearColorValue* clear, uint32 rangeCount, const VkImageSubresourceRange* ranges)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(image);

		// Target state.
		GPUTextureSyncBarrierMasks mask;
		mask.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		mask.barrierMasks.accesMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mask.barrierMasks.queueFamilyIndex = getFamily();

		// Transition before clear.
		for (uint32 rangeIndex = 0; rangeIndex < rangeCount; rangeIndex++)
		{
			image->get().transition(cmd->commandBuffer, mask, ranges[rangeIndex]);
		}

		// Clear image, only work for compute or graphics queue.
		vkCmdClearColorImage(cmd->commandBuffer, image->get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, clear, rangeCount, ranges);
	}

	void GraphicsOrComputeQueue::clearUAV(PoolBufferRef buffer, uint32 data)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(buffer);

		transition(buffer, VK_ACCESS_TRANSFER_WRITE_BIT);
		vkCmdFillBuffer(cmd->commandBuffer, buffer->get(), 0, buffer->get().getSize(), data);
	}

	void GraphicsOrComputeQueue::fillUAV(PoolBufferRef buffer, uint32 offset, uint32 size, uint32 data)
	{
		check(offset + size <= buffer->get().getSize());

		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(buffer);

		transition(buffer, VK_ACCESS_TRANSFER_WRITE_BIT);

		vkCmdFillBuffer(cmd->commandBuffer, buffer->get(), offset, size, data);
	}

	void GraphicsOrComputeQueue::updateUAV(PoolBufferRef buffer, uint32 offset, uint32 size, const void* data)
	{
		check(offset + size <= buffer->get().getSize());

		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(buffer);

		transition(buffer, VK_ACCESS_TRANSFER_WRITE_BIT);

		vkCmdUpdateBuffer(cmd->commandBuffer, buffer->get(), offset, size, data);
	}

	void GraphicsOrComputeQueue::copyUAV(PoolBufferRef src, PoolBufferRef dest, uint srcOffset, uint destOffset, uint size)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(src);
		cmd->addReferenceResource(dest);

		transition(src, VK_ACCESS_TRANSFER_READ_BIT);
		transition(dest, VK_ACCESS_TRANSFER_WRITE_BIT);


		VkBufferCopy copy{};

		check(srcOffset + size < src->get().getSize());
		check(destOffset + size < dest->get().getSize());

		copy.size = size;
		copy.dstOffset = destOffset;
		copy.srcOffset = srcOffset;
		vkCmdCopyBuffer(cmd->commandBuffer, src->get(), dest->get(), 1, &copy);
	}

	void GraphicsQueue::clearDepthStencil(PoolTextureRef image, const VkClearDepthStencilValue* clear)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(image);

		VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (isFormatExistStencilComponent(image->get().getFormat()))
		{
			flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		const auto rangeClearDepth = helper::buildBasicImageSubresource(flags);

		// Target state.
		GPUTextureSyncBarrierMasks mask;
		mask.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		mask.barrierMasks.accesMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mask.barrierMasks.queueFamilyIndex = getFamily();

		image->get().transition(cmd->commandBuffer, mask, rangeClearDepth);

		vkCmdClearDepthStencilImage(cmd->commandBuffer, image->get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, clear, 1, &rangeClearDepth);
	}

	void GraphicsQueue::transitionPresent(PoolTextureRef image)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(image);

		GPUTextureSyncBarrierMasks mask;
		mask.imageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		mask.barrierMasks.accesMask = VK_ACCESS_MEMORY_READ_BIT;
		mask.barrierMasks.queueFamilyIndex = getFamily();

		image->get().transition(cmd->commandBuffer, mask, helper::buildBasicImageSubresource());
	}

	void GraphicsQueue::transitionColorAttachment(PoolTextureRef image)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(image);

		GPUTextureSyncBarrierMasks mask;
		mask.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		mask.barrierMasks.accesMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		mask.barrierMasks.queueFamilyIndex = getFamily();

		image->get().transition(cmd->commandBuffer, mask, helper::buildBasicImageSubresource());
	}

	void GraphicsQueue::transitionDepthStencilAttachment(PoolTextureRef image, EDepthStencilOp op)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(image);

		const bool bExistStencilComponent = isFormatExistStencilComponent(image->get().getFormat());

		GPUTextureSyncBarrierMasks mask;
		mask.imageLayout = getLayoutFromDepthStencilOp(op, bExistStencilComponent);
		mask.barrierMasks.accesMask = getAccessFlagBits(op);
		mask.barrierMasks.queueFamilyIndex = getFamily();

		VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (bExistStencilComponent)
		{
			flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		image->get().transition(cmd->commandBuffer, mask, helper::buildBasicImageSubresource(flags));
	}

	void GraphicsQueue::bindIndexBuffer(PoolBufferRef buffer, VkDeviceSize offset, VkIndexType indexType)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(buffer);

		// transition(buffer, VK_ACCESS_INDEX_READ_BIT);

		vkCmdBindIndexBuffer(cmd->commandBuffer, buffer->get(), offset, indexType);
	}

	void GraphicsQueue::bindVertexBuffer(PoolBufferRef buffer)
	{
		auto cmd = m_activeCmdCtx.command;
		// transition(buffer, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);

		VkBuffer vB = buffer->get();
		const VkDeviceSize vBOffset = 0;

		vkCmdBindVertexBuffers(cmd->commandBuffer, 0, 1, &vB, &vBOffset);
	}

	GraphicsOrComputeQueue::GraphicsOrComputeQueue(const std::string& name, const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family)
		: Queue(name, swapchain, type, queue, family)
	{
	}

	void GraphicsOrComputeQueue::transitionSRV(PoolTextureRef image, VkImageSubresourceRange range)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(image);

		GPUTextureSyncBarrierMasks mask;
		mask.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		mask.barrierMasks.accesMask = VK_ACCESS_SHADER_READ_BIT;
		mask.barrierMasks.queueFamilyIndex = getFamily();

		image->get().transition(cmd->commandBuffer, mask, range);
	}

	void GraphicsOrComputeQueue::transitionUAV(PoolTextureRef image, VkImageSubresourceRange range)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(image);

		GPUTextureSyncBarrierMasks mask;
		mask.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		mask.barrierMasks.accesMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		mask.barrierMasks.queueFamilyIndex = getFamily();

		image->get().transition(cmd->commandBuffer, mask, range);
	}

	void GraphicsOrComputeQueue::transitionUAV(PoolBufferRef buffer)
	{
		transition(buffer, VK_ACCESS_SHADER_WRITE_BIT);
	}

	void GraphicsOrComputeQueue::transitionSRV(PoolBufferRef buffer)
	{
		transition(buffer, VK_ACCESS_SHADER_READ_BIT);
	}

	void GraphicsOrComputeQueue::transition(PoolBufferRef buffer, VkAccessFlags flag)
	{
		auto cmd = m_activeCmdCtx.command;
		cmd->addReferenceResource(buffer);

		GPUSyncBarrierMasks mask;
		mask.accesMask = flag;
		mask.queueFamilyIndex = getFamily();

		auto barrier = buffer->get().updateBarrier(mask);
		helper::pipelineBarrier(cmd->commandBuffer, 0, 1, &barrier, 0, nullptr);
	}

	uint64 CallOnceInOneFrameEvent::m_frameCounter = -1;
	CallOnceEvents<CallOnceInOneFrameEvent, const ApplicationTickData&, graphics::GraphicsQueue&> CallOnceInOneFrameEvent::functions = {};
	void CallOnceInOneFrameEvent::flush(const ApplicationTickData& tickData, graphics::GraphicsQueue& queue)
	{
		if (m_frameCounter == tickData.tickCount)
		{
			return;
		}

		m_frameCounter = tickData.tickCount;
		functions.brocast(tickData, queue);
	}
}

