#pragma once
#include <graphics/common.h>

namespace chord::graphics
{
	class CommandBuffer : NonCopyable
	{
	public:
		explicit CommandBuffer() = default;
		virtual ~CommandBuffer();

		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkCommandPool  pool = VK_NULL_HANDLE;

		// Pending resource for encoded.
		std::vector<ResourceRef> pendingResources;

		uint64 recordingID = 0;
		uint64 submissionID = 0;
	};
	using CommandBufferRef = std::shared_ptr<CommandBuffer>;

	class Queue
	{
	public:
		VkSemaphore trackingSemaphore;

		explicit Queue(EQueueType type, VkQueue queue, uint32 family);
		virtual ~Queue();

		// Create command buffer.
		CommandBufferRef createCommandBuffer();
		CommandBufferRef getOrCreateCommandBuffer();

		void addWaitSemaphore(VkSemaphore semaphore, uint64 value);
		void addSignalSemaphore(VkSemaphore semaphore, uint64 value);

	private:
		mutable std::mutex m_mutex;

		VkQueue    m_queue;
		EQueueType m_queueType;
		uint32     m_queueFamily;

		std::vector<VkSemaphore> m_waitSemaphores;
		std::vector<uint64> m_waitSemaphoreValues;

		std::vector<VkSemaphore> m_signalSemaphores;
		std::vector<uint64> m_signalSemaphoreValues;

		uint64 m_lastRecordingID = 0;
		uint64 m_lastSubmittedID = 0;
		uint64 m_lastFinishedID  = 0;

		std::list<CommandBufferRef> m_commandBuffersInFlight;
		std::list<CommandBufferRef> m_commandBuffersPool;
	};

	// Command list control command open and closed.
	class CommandList : NonCopyable
	{
	public:
		

	private:
		// Current command buffer.
		CommandBufferRef m_commandBuffer;


	};
}