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
			virtual ~PoolBuffer()
			{
				m_pool.m_buffers[m_hashId].push(m_buffer);
			}

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
			VkBufferCreateFlags flags,
			VkDeviceSize size,
			VkBufferUsageFlags usage);

		class HostVisiblePoolBuffer : public PoolBuffer
		{
			friend GPUBufferPool;
		public:
			explicit HostVisiblePoolBuffer(HostVisibleGPUBufferRef inBuffer, uint64 hashId, GPUBufferPool& pool)
				: PoolBuffer(inBuffer, hashId, pool)
			{
			}

			operator const HostVisibleGPUBuffer& () const { return *((HostVisibleGPUBuffer*)m_buffer.get()); }
			operator HostVisibleGPUBuffer& () { return *((HostVisibleGPUBuffer*)m_buffer.get()); }
		};

		std::shared_ptr<HostVisiblePoolBuffer> createHostVisible(
			const std::string& name,
			VkBufferCreateFlags flags,
			VkBufferUsageFlags usage,
			SizedBuffer data);

	private:
		friend PoolBuffer;

		std::map<uint64, std::stack<GPUBufferRef>> m_buffers;
		std::map<uint64, std::stack<GPUOnlyBufferRef>> m_gpuOnlyBuffers;
		std::map<uint64, std::stack<HostVisibleGPUBufferRef>> m_hostVisibleBuffers;
	};
	using GPUBufferPoolRef = std::shared_ptr<GPUBufferPool>;
	using PoolBufferRef = std::shared_ptr<GPUBufferPool::PoolBuffer>;
	using PoolBufferGPUOnlyRef = std::shared_ptr<GPUBufferPool::GPUOnlyPoolBuffer>;
	using PoolBufferHostVisible = std::shared_ptr<GPUBufferPool::HostVisiblePoolBuffer>;
}