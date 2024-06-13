#pragma once
#include <graphics/common.h>
#include <list>
#include <graphics/resource.h>

namespace chord::graphics
{
	class Swapchain;
	struct CommandBuffer : NonCopyable
	{
		explicit CommandBuffer() = default;
		virtual ~CommandBuffer();

		// All pending resources.
		std::set<ResourceRef> pendingResources;

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
		explicit Queue(const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family);
		virtual ~Queue();

		void checkRecording() const;

		void beginCommand(const std::vector<TimelineWait>& waitValue);
		TimelineWait endCommand();

		// Sync when a global fence finish.
		void sync(uint32 freeFrameCount);

		TimelineWait getCurrentTimeline() const
		{
			return TimelineWait{.timeline = m_timelineSemaphore, .waitValue = m_timelineValue };
		}

		TimelineWait stepTimeline()
		{
			m_timelineValue ++;
			return getCurrentTimeline();
		}

		CommandBufferRef getActiveCmd() const
		{
			return m_activeCmdCtx.command;
		}

		uint32 getFamily() const { return m_queueFamily; }

	private:
		CommandBufferRef getOrCreateCommandBuffer();



	protected:
		const Swapchain& m_swapchain;

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

	class GraphicsOrComputeQueue : public Queue
	{
	public:
		explicit GraphicsOrComputeQueue(const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family);

		void transitionSRV(GPUTextureRef image, VkImageSubresourceRange range);

		void clearImage(GPUTextureRef image, const VkClearColorValue* clear, uint32 rangeCount, const VkImageSubresourceRange* ranges);
	};



	class GraphicsQueue : public GraphicsOrComputeQueue
	{
	public:
		explicit GraphicsQueue(const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family);

		
		void clearDepthStencil(
			GPUTextureRef image, 
			const VkClearDepthStencilValue* clear, 
			VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

		void transitionPresent(GPUTextureRef image);

		void transitionColorAttachment(GPUTextureRef image);
		void transitionDepthStencilAttachment(GPUTextureRef image, EDepthStencilOp op);
	};

	// Command list control command open and closed.
	class CommandList : NonCopyable
	{
	public:
		explicit CommandList(const Swapchain& swapchain);
		virtual ~CommandList();

		// Sync when a global fence finish.
		void sync(uint32 freeFrameCount);

		GraphicsQueue& getGraphicsQueue() const { return *m_graphicsQueue; }

		Queue& getQueue(EQueueType type)
		{
			switch (type)
			{
			case EQueueType::Graphics: return *m_graphicsQueue;
			case EQueueType::Compute:  return *m_asyncComputeQueue;
			case EQueueType::Copy:     return *m_asyncCopyQueue;
			}

			checkEntry();
			return *m_graphicsQueue;
		}

	private:
		const Swapchain& m_swapchain;

		std::unique_ptr<GraphicsQueue> m_graphicsQueue;
		std::unique_ptr<Queue> m_asyncComputeQueue;
		std::unique_ptr<Queue> m_asyncCopyQueue;
	};
}