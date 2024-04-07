#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/command.h>

namespace chord::graphics
{
	Queue::Queue(EQueueType type, VkQueue queue, uint32 family)
		: m_queue(queue)
		, m_queueType(type)
		, m_queueFamily(family)
	{
		trackingSemaphore = helper::createTimelineSemaphore();
	}

	Queue::~Queue()
	{
		helper::destroySemaphore(trackingSemaphore);
	}

	CommandBufferRef Queue::createCommandBuffer()
	{
		auto result = std::make_shared<CommandBuffer>();

		result->pool = helper::createCommandPool(m_queueFamily,
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

		result->cmd = helper::allocateCommandBuffer(result->pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

		return result;
	}

	CommandBufferRef Queue::getOrCreateCommandBuffer()
	{
		std::lock_guard lock(m_mutex);

		uint64 recordingID = ++m_lastRecordingID;

		CommandBufferRef cmdBuf;
		if (m_commandBuffersPool.empty())
		{
			cmdBuf = createCommandBuffer();
		}
		else
		{
			cmdBuf = m_commandBuffersPool.front();
			m_commandBuffersPool.pop_front();
		}

		cmdBuf->recordingID = recordingID;
		return cmdBuf;
	}

	void Queue::addWaitSemaphore(VkSemaphore semaphore, uint64 value)
	{
		m_waitSemaphores.push_back(semaphore);
		m_waitSemaphoreValues.push_back(value);
	}

	void Queue::addSignalSemaphore(VkSemaphore semaphore, uint64 value)
	{
		m_signalSemaphores.push_back(semaphore);
		m_signalSemaphoreValues.push_back(value);
	}
}