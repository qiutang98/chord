#pragma once
#include <graphics/common.h>
#include <list>

namespace chord::graphics
{
	struct CommandBuffer : NonCopyable
	{
		explicit CommandBuffer() = default;
		virtual ~CommandBuffer();

		// All pending resources.
		std::vector<ResourceRef> pendingResources;

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkCommandPool   commandPool   = VK_NULL_HANDLE;

		uint64 signalValue = 0;
	};
	using CommandBufferRef = std::shared_ptr<CommandBuffer>;

	struct TimelineWait
	{
		VkSemaphore timeline;
		uint64 waitValue;
	};

	class Queue : NonCopyable
	{
	public:
		explicit Queue(EQueueType type, VkQueue queue, uint32 family);
		virtual ~Queue();

		void beginCommand(const std::vector<TimelineWait>& waitValue);
		TimelineWait endCommand();

		// Sync when a global fence finish.
		void sync();

	private:
		CommandBufferRef getOrCreateCommandBuffer();



	private:
		VkQueue    m_queue;
		EQueueType m_queueType;
		uint32     m_queueFamily;

		std::list<CommandBufferRef> m_usingCommands;
		std::list<CommandBufferRef> m_commandsPool;

		uint64 m_timelineValue = 0;
		VkSemaphore m_timelineSemaphore = VK_NULL_HANDLE;

		struct ActiveCmd
		{
			CommandBufferRef command = nullptr;
			std::vector<TimelineWait> waitValue;
		} m_activeCmdCtx;
	};

	// Command list control command open and closed.
	class CommandList : NonCopyable
	{
	public:
		explicit CommandList();
		virtual ~CommandList();

		// Sync when a global fence finish.
		void sync();

		void beginGraphicsCommand(const std::vector<TimelineWait>& waitValue);
		TimelineWait endGraphicsCommand();

		void beginAsyncComputeCommand(const std::vector<TimelineWait>& waitValue);
		TimelineWait endAsyncComputeCommand();

		void beginAsyncCopyCommand(const std::vector<TimelineWait>& waitValue);
		TimelineWait endAsyncCopyCommand();

	private:
		std::unique_ptr<Queue> m_graphicsQueue;
		std::unique_ptr<Queue> m_asyncComputeQueue;
		std::unique_ptr<Queue> m_asyncCopyQueue;
	};
}