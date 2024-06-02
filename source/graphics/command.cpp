#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/command.h>


namespace chord::graphics
{

	CommandBuffer::~CommandBuffer()
	{
		// Release pool, self management memory.
		helper::destroyCommandPool(commandPool);
	}

	Queue::Queue(EQueueType type, VkQueue queue, uint32 family)
		: m_queue(queue)
		, m_queueType(type)
		, m_queueFamily(family)
	{
		m_timelineValue = 0;
		m_timelineSemaphore = helper::createTimelineSemaphore(m_timelineValue);

	}

	Queue::~Queue()
	{
		helper::destroySemaphore(m_timelineSemaphore);
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

	void Queue::beginCommand(const std::vector<TimelineWait>& waitValue)
	{
		check(m_activeCmdCtx.command == nullptr);

		m_activeCmdCtx.command = getOrCreateCommandBuffer();
		m_activeCmdCtx.waitValue = waitValue;
	}

	TimelineWait Queue::endCommand()
	{
		check(m_activeCmdCtx.command != nullptr);

		// Signal value increment.
		m_timelineValue++;
		m_activeCmdCtx.command->signalValue = m_timelineValue;

		std::vector<uint64> waitValues{};
		std::vector<VkSemaphore> waitSemaphores{};
		for (auto& wait : m_activeCmdCtx.waitValue)
		{
			waitValues.push_back(wait.waitValue);
			waitSemaphores.push_back(wait.timeline);
		}

		VkTimelineSemaphoreSubmitInfo timelineInfo;
		timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
		timelineInfo.pNext = NULL;
		timelineInfo.waitSemaphoreValueCount = waitValues.size();
		timelineInfo.pWaitSemaphoreValues = waitValues.data();
		timelineInfo.signalSemaphoreValueCount = 1;
		timelineInfo.pSignalSemaphoreValues = &m_activeCmdCtx.command->signalValue;

		VkSubmitInfo submitInfo;
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pNext = &timelineInfo;
		submitInfo.waitSemaphoreCount = waitSemaphores.size();
		submitInfo.pWaitSemaphores = waitSemaphores.data();
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &m_timelineSemaphore;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_activeCmdCtx.command->commandBuffer;

		vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE);

		m_usingCommands.push_back(m_activeCmdCtx.command);
		m_activeCmdCtx = {};

		return TimelineWait{ .timeline = m_timelineSemaphore, .waitValue = m_timelineValue };
	}

	void Queue::sync()
	{
		const uint64 waitValue = m_timelineValue;

		// Free command when finish.
		if (!m_usingCommands.empty())
		{
			uint64 currentValue;
			vkGetSemaphoreCounterValue(getDevice(), m_timelineSemaphore, &currentValue);

			check(waitValue <= currentValue);

			auto usingCmd = m_usingCommands.begin();
			uint64 usingTimelineValue = (*usingCmd)->signalValue;
			while (usingCmd != m_usingCommands.end() && usingTimelineValue <= currentValue)
			{
				m_commandsPool.push_back(*usingCmd);
				usingCmd = m_usingCommands.erase(usingCmd);
			}
		}
	}

	CommandList::CommandList()
	{
		const auto& queueInfos = getContext().getQueuesInfo();
		m_graphicsQueue = std::make_unique<Queue>(EQueueType::Graphics, queueInfos.graphcisQueues[0].queue, queueInfos.graphicsFamily.get());
		m_asyncComputeQueue = std::make_unique<Queue>(EQueueType::Compute, queueInfos.computeQueues[0].queue, queueInfos.computeFamily.get());
		m_asyncCopyQueue = std::make_unique<Queue>(EQueueType::Copy, queueInfos.copyQueues[0].queue, queueInfos.copyFamily.get());
	}

	CommandList::~CommandList()
	{

	}

	void CommandList::sync()
	{
		m_graphicsQueue->sync();
		m_asyncComputeQueue->sync();
		m_asyncCopyQueue->sync();
	}

	void CommandList::beginGraphicsCommand(const std::vector<TimelineWait>& waitValue)
	{
		m_graphicsQueue->beginCommand(waitValue);
	}

	void CommandList::beginAsyncComputeCommand(const std::vector<TimelineWait>& waitValue)
	{
		m_asyncComputeQueue->beginCommand(waitValue);
	}

	void CommandList::beginAsyncCopyCommand(const std::vector<TimelineWait>& waitValue)
	{
		m_asyncCopyQueue->beginCommand(waitValue);
	}

	TimelineWait CommandList::endGraphicsCommand()
	{
		return m_graphicsQueue->endCommand();
	}

	TimelineWait CommandList::endAsyncComputeCommand()
	{
		return m_graphicsQueue->endCommand();
	}

	TimelineWait CommandList::endAsyncCopyCommand()
	{
		return m_graphicsQueue->endCommand();
	}
}

