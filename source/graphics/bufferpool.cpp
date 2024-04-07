#include <graphics/bufferpool.h>
#include <graphics/graphics.h>
#include <graphics/resource.h>
#include <utils/cityhash.h>

namespace chord::graphics
{
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
			buffer = list.top();
			buffer->rename(name);
			list.pop();
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
		auto& list = m_gpuOnlyBuffers[hashId];

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
			buffer = list.top();
			buffer->rename(name);
			list.pop();
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
		auto& list = m_hostVisibleBuffers[hashId];

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
			buffer = list.top();
			buffer->rename(name);
			list.pop();
		}

		return std::make_shared<GPUBufferPool::HostVisiblePoolBuffer>(buffer, hashId, *this);
	}
}

