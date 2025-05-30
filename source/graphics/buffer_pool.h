#pragma once

#include <graphics/resource.h>

namespace chord::graphics
{
	struct PoolBufferCreateInfo
	{
		VkBufferCreateFlags flags;
		VkDeviceSize        size;
		VkBufferUsageFlags  usage;

		VmaAllocationCreateFlags vmaCreateFlag;
	};

	class GPUBufferPool : NonCopyable
	{
	public:

		class PoolBuffer : public IPoolResource
		{
			friend GPUBufferPool;
		public:
			virtual ~PoolBuffer();

			explicit PoolBuffer(GPUBufferRef inBuffer, uint64 hashId, GPUBufferPool& pool, bool bSameFrameReuse = true)
				: IPoolResource(bSameFrameReuse)
				, m_buffer(inBuffer)
				, m_hashId(hashId)
				, m_pool(pool)
			{
			}

			const GPUBuffer& get() const { return *m_buffer; }
			GPUBuffer& get() { return *m_buffer; }

			virtual GPUResourceRef getGPUResourceRef() const override
			{
				return m_buffer;
			}

			GPUBufferRef getGPUBufferRef() const { return m_buffer; }

		protected:
			const uint64 m_hashId;
			GPUBufferPool& m_pool;
			GPUBufferRef m_buffer = nullptr;
		};
		
		explicit GPUBufferPool(uint64 freeCount)
			: m_freeFrameCount(freeCount)
		{

		}

		virtual ~GPUBufferPool();

		void garbageCollected(const ApplicationTickData& tickData);

		// Generic buffer.
		std::shared_ptr<PoolBuffer> create(
			const std::string& name,
			const PoolBufferCreateInfo& info,
			bool bSameFrameReuse = true);

		class GPUOnlyPoolBuffer : public PoolBuffer
		{
			friend GPUBufferPool;
		public:
			explicit GPUOnlyPoolBuffer(GPUOnlyBufferRef inBuffer, uint64 hashId, GPUBufferPool& pool, bool bSameFrameReuse = true)
				: PoolBuffer(inBuffer, hashId, pool, bSameFrameReuse)
			{
			}

			virtual ~GPUOnlyPoolBuffer() { }
		};

		std::shared_ptr<GPUOnlyPoolBuffer> createGPUOnly(
			const std::string& name,
			VkDeviceSize size,
			VkBufferUsageFlags usage,
			VkBufferCreateFlags flags = 0,
			bool bSameFrameReuse = true);

		class HostVisiblePoolBuffer : public PoolBuffer
		{
			friend GPUBufferPool;
		public:
			explicit HostVisiblePoolBuffer(HostVisibleGPUBufferRef inBuffer, uint64 hashId, GPUBufferPool& pool)
				: PoolBuffer(inBuffer, hashId, pool, false) // Host visible buffer can't be reused in same frame.
			{
			}

			virtual ~HostVisiblePoolBuffer() { }

			const HostVisibleGPUBuffer& get() const { return *((HostVisibleGPUBuffer*)m_buffer.get()); }
			HostVisibleGPUBuffer& get() { return *((HostVisibleGPUBuffer*)m_buffer.get()); }

			uint32 getBindlessIndex() const;
		};

		std::shared_ptr<HostVisiblePoolBuffer> createHostVisible(
			const std::string& name,
			VkBufferUsageFlags usage,
			SizedBuffer data,
			VmaAllocationCreateInfo vmaAllocationInfo,
			VkBufferCreateFlags flags = 0);

		std::shared_ptr<HostVisiblePoolBuffer> createHostVisibleCopyUpload(
			const std::string& name,
			VkBufferUsageFlags usage,
			SizedBuffer data,
			VkBufferCreateFlags flags = 0);

		std::shared_ptr<HostVisiblePoolBuffer> createHostVisibleReadBack(
			const std::string& name,
			VkDeviceSize size,
			VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VkBufferCreateFlags flags = 0);

	private:
		struct FreePoolBuffer
		{
			GPUBufferRef buffer;
			uint64 freeFrame = 0;
		};

		void recentUsedUpdate(std::vector<FreePoolBuffer>& buffers) const;

		friend PoolBuffer;

		std::mutex m_mutex;

		// Free frame count.
		const uint64 m_freeFrameCount;

		// Counter inner.
		uint64 m_frameCounter = 0;
		std::map<uint64, std::vector<FreePoolBuffer>> m_buffers;
	};
	using GPUBufferPoolRef = std::shared_ptr<GPUBufferPool>;
	using PoolBufferRef = std::shared_ptr<GPUBufferPool::PoolBuffer>;
	using PoolBufferGPUOnlyRef = std::shared_ptr<GPUBufferPool::GPUOnlyPoolBuffer>;
	using PoolBufferHostVisible = std::shared_ptr<GPUBufferPool::HostVisiblePoolBuffer>;
}