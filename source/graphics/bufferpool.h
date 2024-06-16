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
		class PoolBuffer : public IResource
		{
			friend GPUBufferPool;
		public:
			virtual ~PoolBuffer();

			explicit PoolBuffer(GPUBufferRef inBuffer, uint64 hashId, GPUBufferPool& pool)
				: m_buffer(inBuffer)
				, m_hashId(hashId)
				, m_pool(pool)
			{
			}

		protected:
			operator const GPUBuffer& () const { return *m_buffer; }
			operator GPUBuffer& () { return *m_buffer; }
			 
			const uint64 m_hashId;
			GPUBufferPool& m_pool;
			GPUBufferRef m_buffer = nullptr;
		};
		
		explicit GPUBufferPool(uint64 freeCount)
			: m_freeFrameCount(freeCount)
		{

		}

		virtual ~GPUBufferPool();

		void tick(const ApplicationTickData& tickData);

		// Generic buffer.
		std::shared_ptr<PoolBuffer> create(
			const std::string& name,
			const PoolBufferCreateInfo& info);

		class GPUOnlyPoolBuffer : public PoolBuffer
		{
			friend GPUBufferPool;
		public:
			explicit GPUOnlyPoolBuffer(GPUOnlyBufferRef inBuffer, uint64 hashId, GPUBufferPool& pool)
				: PoolBuffer(inBuffer, hashId, pool)
			{
			}

			operator const GPUOnlyBuffer& () const { return *((GPUOnlyBuffer*)m_buffer.get()); }
			operator GPUOnlyBuffer& () { return *((GPUOnlyBuffer*)m_buffer.get()); }
		};

		std::shared_ptr<GPUOnlyPoolBuffer> createGPUOnly(
			const std::string& name,
			VkDeviceSize size,
			VkBufferUsageFlags usage,
			VkBufferCreateFlags flags = 0);

		class HostVisiblePoolBuffer : public PoolBuffer
		{
			friend GPUBufferPool;
		public:
			explicit HostVisiblePoolBuffer(HostVisibleGPUBufferRef inBuffer, uint64 hashId, GPUBufferPool& pool)
				: PoolBuffer(inBuffer, hashId, pool)
			{
			}

			const HostVisibleGPUBuffer& get() const { return *((HostVisibleGPUBuffer*)m_buffer.get()); }
			HostVisibleGPUBuffer& get() { return *((HostVisibleGPUBuffer*)m_buffer.get()); }
		};

		std::shared_ptr<HostVisiblePoolBuffer> createHostVisible(
			const std::string& name,
			VkBufferUsageFlags usage,
			SizedBuffer data,
			VkBufferCreateFlags flags = 0);

	private:
		friend PoolBuffer;

		// Free frame count.
		const uint64 m_freeFrameCount;

		// Counter inner.
		uint64 m_frameCounter = 0;

		struct FreePoolBuffer
		{
			GPUBufferRef buffer;
			uint64 freeFrame = 0;
		};
		std::map<uint64, std::vector<FreePoolBuffer>> m_buffers;
	};
	using GPUBufferPoolRef = std::shared_ptr<GPUBufferPool>;
	using PoolBufferRef = std::shared_ptr<GPUBufferPool::PoolBuffer>;
	using PoolBufferGPUOnlyRef = std::shared_ptr<GPUBufferPool::GPUOnlyPoolBuffer>;
	using PoolBufferHostVisible = std::shared_ptr<GPUBufferPool::HostVisiblePoolBuffer>;
}