#pragma once
#include <graphics/common.h>
#include <utils/mpsc_queue.h>
#include <graphics/resource.h>
#include <graphics/buffer_pool.h>
#include <graphics/texture_pool.h>

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
		void addReferenceResource(std::shared_ptr<T> resource)
		{
			static_assert(std::is_base_of_v<IResource, T>);
			if constexpr (std::is_base_of_v<IPoolResource, T>)
			{
				auto ptr = std::dynamic_pointer_cast<IPoolResource>(resource);
				if (ptr->shouldSameFrameReuse())
				{
					m_referenceResources.push_back(ptr->getGPUResourceRef());
					return;
				}
			}

			m_referenceResources.push_back(resource);
		}

		void clearReferenceResources()
		{
			m_referenceResources.clear();
		}

		bool isReferenceResourcesEmpty() const
		{
			return m_referenceResources.empty();
		}

	private:
		// All pending resources.
		std::vector<ResourceRef> m_referenceResources;
	};
	using CommandBufferRef = std::shared_ptr<CommandBuffer>;

	struct QueueTimeline
	{
		VkSemaphore semaphore;

		// Last time submit command signal value.
		uint64 waitValue = 0U;

		// Last time submit command required wait flags.
		VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		void waitFinish() const;
	};

	class Queue : NonCopyable
	{
	public:
		explicit Queue(const std::string& name, const Swapchain& swapchain, EQueueType type, VkQueue queue, uint32 family);
		virtual ~Queue();

		void checkRecording() const;

		void beginCommand(const std::vector<QueueTimeline>& waitValue);
		QueueTimeline endCommand(VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		// Sync when a global fence finish.
		void sync(uint32 freeFrameCount);

		QueueTimeline getCurrentTimeline() const
		{
			return m_timeline;
		}

		QueueTimeline stepTimeline(VkPipelineStageFlags waitFlags)
		{
			m_timeline.waitValue ++;
			m_timeline.waitFlags = waitFlags;
			return getCurrentTimeline();
		}

		CommandBufferRef getActiveCmd() const
		{
			return m_activeCmdCtx.command;
		}

		uint32 getFamily() const { return m_queueFamily; }

	public:
		void copyBuffer(PoolBufferRef src, PoolBufferRef dest, size_t size, size_t srcOffset = 0, size_t destOffset = 0);
		PoolBufferHostVisible copyImageToReadBackBuffer(PoolTextureRef src);
		void uploadTexture(PoolTextureRef dest, SizedBuffer buffer);

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

		QueueTimeline m_timeline { };

		struct ActiveCmd
		{
			CommandBufferRef command = nullptr;
			std::vector<QueueTimeline> waitValue;
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

		void addReferenceResource(ResourceRef resource);

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
		static uint32 m_usingIndex;
		static std::vector<std::function<void(const ApplicationTickData&, graphics::GraphicsQueue&)>> m_functions[2];

	public:
		static void add(std::function<void(const ApplicationTickData&, graphics::GraphicsQueue&)>&& func);

		// 
		static void flush(const ApplicationTickData& tickData, graphics::GraphicsQueue& queue);

		//
		static void clean();
	};
}