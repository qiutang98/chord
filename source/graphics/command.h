#pragma once
#include <graphics/common.h>
#include <list>
#include <graphics/resource.h>
#include <graphics/bufferpool.h>
#include <graphics/rendertargetpool.h>

namespace chord::graphics
{
	class Swapchain;
	class CommandBuffer : NonCopyable
	{
	public:
		explicit CommandBuffer() = default;
		virtual ~CommandBuffer();

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkCommandPool   commandPool   = VK_NULL_HANDLE;

		//
		uint64 signalValue = 0;

		template<typename T>
		void insertPendingResource(std::shared_ptr<T> resource)
		{
			static_assert(std::is_base_of_v<IResource, T>);
			if constexpr (std::is_base_of_v<GPUTexturePool::PoolTexture, T>)
			{
				auto ptr = std::dynamic_pointer_cast<GPUTexturePool::PoolTexture>(resource);
				if (ptr->shouldSameFrameReuse())
				{
					m_pendingResources.insert(ptr->getGPUResource());
					return;
				}
			}

			if constexpr (std::is_base_of_v<GPUBufferPool::PoolBuffer, T>)
			{
				auto ptr = std::dynamic_pointer_cast<GPUBufferPool::PoolBuffer>(resource);
				if (ptr->shouldSameFrameReuse())
				{
					m_pendingResources.insert(ptr->getGPUResource());
					return;
				}
			}

			m_pendingResources.insert(resource);
		}

		void clearPendingResource()
		{
			m_pendingResources.clear();
		}

		bool isPendingResourceEmpty() const
		{
			return m_pendingResources.empty();
		}

	private:
		// All pending resources.
		std::set<ResourceRef> m_pendingResources;
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
		explicit Queue(const std::string& name, const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family);
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

	public:
		void copyBuffer(PoolBufferRef src, PoolBufferRef dest, size_t size, size_t srcOffset = 0, size_t destOffset = 0);

	private:
		CommandBufferRef getOrCreateCommandBuffer();

	protected:
		const std::string m_name;
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
		explicit GraphicsOrComputeQueue(const std::string& name, const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family);
		virtual ~GraphicsOrComputeQueue() { }

		void transitionSRV(PoolTextureRef image, VkImageSubresourceRange range);
		void transitionUAV(PoolTextureRef image, VkImageSubresourceRange range);
		void transitionUAV(PoolBufferRef buffer);
		void transitionSRV(PoolBufferRef buffer);
		void transition(PoolBufferRef buffer, VkAccessFlags flag);
		void clearImage(PoolTextureRef image, const VkClearColorValue* clear, uint32 rangeCount, const VkImageSubresourceRange* ranges);
		void clearUAV(PoolBufferRef buffer, uint32 data = 0U);
		void fillUAV(PoolBufferRef buffer, uint32 offset, uint32 size, uint32 data);
		void updateUAV(PoolBufferRef buffer, uint32 offset, uint32 size, const void* data);
		void copyUAV(PoolBufferRef src, PoolBufferRef dest, uint32 srcOffset, uint32 destOffset, uint32 size);
	};

	class GraphicsQueue : public GraphicsOrComputeQueue
	{
	public:
		explicit GraphicsQueue(const std::string& name, const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family);
		virtual ~GraphicsQueue() { }
		
		void clearDepthStencil(PoolTextureRef image, const VkClearDepthStencilValue* clear);

		void transitionPresent(PoolTextureRef image);

		void transitionColorAttachment(PoolTextureRef image);
		void transitionDepthStencilAttachment(PoolTextureRef image, EDepthStencilOp op);

		void bindIndexBuffer(PoolBufferRef buffer, VkDeviceSize offset, VkIndexType indexType = VK_INDEX_TYPE_UINT32);
		void bindVertexBuffer(PoolBufferRef buffer);
	};

	// Command list control command open and closed.
	class CommandList : NonCopyable
	{
	public:
		explicit CommandList(Swapchain& swapchain);
		virtual ~CommandList();

		// Sync when a global fence finish.
		void sync(uint32 freeFrameCount);

		GraphicsQueue& getGraphicsQueue() const { return *m_graphicsQueue; }
		GraphicsOrComputeQueue& getAsyncComputeQueue() const { return *m_asyncComputeQueue; }

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

		void insertPendingResource(ResourceRef resource);

	private:
		Swapchain& m_swapchain;

		std::unique_ptr<GraphicsQueue> m_graphicsQueue;
		std::unique_ptr<GraphicsOrComputeQueue> m_asyncComputeQueue;
		std::unique_ptr<Queue> m_asyncCopyQueue;
	};

	class CallOnceInOneFrameEvent
	{
	private:
		static uint64 m_frameCounter;

	public:
		static CallOnceEvents<CallOnceInOneFrameEvent, const ApplicationTickData&, graphics::GraphicsQueue&> functions;
		static void flush(const ApplicationTickData& tickData, graphics::GraphicsQueue& queue);
	};
}