#include <graphics/bufferpool.h>
#include <graphics/graphics.h>
#include <graphics/resource.h>
#include <utils/cityhash.h>
#include <application/application.h>

namespace chord::graphics
{
	GPUBufferPool::~GPUBufferPool()
	{
		m_buffers.clear();
	}

	void GPUBufferPool::tick(const ApplicationTickData& tickData)
	{
		// Update inner counter.
		m_frameCounter = tickData.tickCount;

		// Clear garbages.
		std::vector<uint64> emptyKeys;
		for (auto& buffersPair : m_buffers)
		{
			const auto& key = buffersPair.first;
			auto& buffers  = buffersPair.second;

			buffers.erase(std::remove_if(buffers.begin(), buffers.end(), [&](const auto& t)
			{
				return m_frameCounter - t.freeFrame > m_freeFrameCount;
			}), buffers.end());

			if (buffers.empty())
			{
				emptyKeys.push_back(key);
			}
		}

		for (const auto& key : emptyKeys)
		{
			m_buffers.erase(key);
		}
	}

	PoolBufferRef GPUBufferPool::create(const std::string& name, const PoolBufferCreateInfo& info)
	{
		const uint64 hashId = cityhash::cityhash64((const char*)&info, sizeof(info));
		auto& list = m_buffers[hashId];

		GPUBufferRef buffer = nullptr;
		if (list.empty())
		{
			VkBufferCreateInfo ci{};
			ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			ci.size  = info.size;
			ci.usage = info.usage;
			ci.flags = info.flags;
			ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VmaAllocationCreateInfo vmaallocInfo{};
			vmaallocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			vmaallocInfo.flags = info.flags;

			buffer = std::make_shared<GPUBuffer>(name, ci, vmaallocInfo);
		}
		else
		{
			buffer = list.back().buffer;
			buffer->rename(name);
			list.pop_back();
		}

		return std::make_shared<GPUBufferPool::PoolBuffer>(buffer, hashId, *this);
	}

	PoolBufferGPUOnlyRef GPUBufferPool::createGPUOnly(
		const std::string& name,
		VkBufferCreateFlags flags,
		VkDeviceSize size,
		VkBufferUsageFlags usage)
	{
		PoolBufferCreateInfo poolInfo{};
		poolInfo.flags = flags;
		poolInfo.size  = size;
		poolInfo.usage = usage;
		poolInfo.vmaCreateFlag = getGPUOnlyBufferVMACI().flags;

		const uint64 hashId = cityhash::cityhash64((const char*)&poolInfo, sizeof(poolInfo));
		auto& list = m_buffers[hashId];

		GPUOnlyBufferRef buffer = nullptr;
		if (list.empty())
		{
			VkBufferCreateInfo ci{};
			ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			ci.size  = poolInfo.size;
			ci.usage = poolInfo.usage;
			ci.flags = poolInfo.flags;
			ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			buffer = std::make_shared<GPUOnlyBuffer>(name, ci);
		}
		else
		{
			buffer = std::dynamic_pointer_cast<GPUOnlyBuffer>(list.back().buffer);
			buffer->rename(name);
			list.pop_back();
		}

		return std::make_shared<GPUBufferPool::GPUOnlyPoolBuffer>(buffer, hashId, *this);
	}

	PoolBufferHostVisible GPUBufferPool::createHostVisible(
		const std::string& name,
		VkBufferCreateFlags flags, 
		VkBufferUsageFlags usage,
		SizedBuffer data)
	{
		PoolBufferCreateInfo poolInfo{};
		poolInfo.flags = flags;
		poolInfo.size  = data.size;
		poolInfo.usage = usage;
		poolInfo.vmaCreateFlag = getHostVisibleGPUBufferVMACI().flags;

		const uint64 hashId = cityhash::cityhash64((const char*)&poolInfo, sizeof(poolInfo));
		auto& list = m_buffers[hashId];

		HostVisibleGPUBufferRef buffer = nullptr;
		if (list.empty())
		{
			VkBufferCreateInfo ci{};
			ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			ci.size  = poolInfo.size;
			ci.usage = poolInfo.usage;
			ci.flags = poolInfo.flags;
			ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			buffer = std::make_shared<HostVisibleGPUBuffer>(name, ci, data);
		}
		else
		{
			buffer = std::dynamic_pointer_cast<HostVisibleGPUBuffer>(list.back().buffer);
			buffer->rename(name);
			list.pop_back();
		}

		return std::make_shared<GPUBufferPool::HostVisiblePoolBuffer>(buffer, hashId, *this);
	}

	GPUBufferPool::PoolBuffer::~PoolBuffer()
	{
		FreePoolBuffer freeBuffer;
		freeBuffer.buffer = m_buffer;
		freeBuffer.freeFrame = m_pool.m_frameCounter;

		m_pool.m_buffers[m_hashId].push_back(freeBuffer);
	}
}

