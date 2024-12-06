#include <graphics/bufferpool.h>
#include <graphics/graphics.h>
#include <graphics/resource.h>
#include <utils/cityhash.h>
#include <application/application.h>

namespace chord::graphics
{
	constexpr uint64 kBufferSizeAllocRound = 256;

	GPUBufferPool::~GPUBufferPool()
	{
		m_buffers.clear();
	}

	void GPUBufferPool::garbageCollected(const ApplicationTickData& tickData)
	{
		// Update inner counter.
		m_frameCounter = tickData.tickCount;

		// Clear garbages.
		std::vector<uint64> emptyKeys;
		for (auto& buffersPair : m_buffers)
		{
			const auto& key = buffersPair.first;
			auto& buffers  = buffersPair.second;

			if (!buffers.empty())
			{
				buffers.erase(std::remove_if(buffers.begin(), buffers.end(), [&](const auto& t)
				{
					return m_frameCounter - t.freeFrame > m_freeFrameCount;
				}), buffers.end());
			}

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

	void GPUBufferPool::recentUsedUpdate(std::vector<FreePoolBuffer>& buffers) const
	{
		if (!buffers.empty())
		{
			for (auto& buffer : buffers)
			{
				buffer.freeFrame = math::max(buffer.freeFrame, m_frameCounter - m_freeFrameCount + 1);
			}
		}
	}

	PoolBufferRef GPUBufferPool::create(const std::string& name, const PoolBufferCreateInfo& inputInfo, bool bSameFrameReuse)
	{
		auto info = inputInfo;
		info.size = divideRoundingUp(info.size, kBufferSizeAllocRound) * kBufferSizeAllocRound;

		const uint64 hashId = cityhash::cityhash64((const char*)&info, sizeof(info));
		auto& freeBuffers = m_buffers[hashId];

		GPUBufferRef buffer = nullptr;
		if (freeBuffers.empty())
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
			std::swap(freeBuffers.front(), freeBuffers.back()); // Use the oldest gay.

			buffer = freeBuffers.back().buffer;
			buffer->rename(name, false);
			freeBuffers.pop_back();

			recentUsedUpdate(freeBuffers);
		}

		return std::make_shared<GPUBufferPool::PoolBuffer>(buffer, hashId, *this, bSameFrameReuse);
	}

	PoolBufferGPUOnlyRef GPUBufferPool::createGPUOnly(
		const std::string& name,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkBufferCreateFlags flags,
		bool bSameFrameReuse)
	{
		PoolBufferCreateInfo poolInfo{};
		poolInfo.flags = flags;
		poolInfo.size  = divideRoundingUp(size, kBufferSizeAllocRound) * kBufferSizeAllocRound;
		poolInfo.usage = usage;
		poolInfo.vmaCreateFlag = getGPUOnlyBufferVMACI().flags;

		const uint64 hashId = cityhash::cityhash64((const char*)&poolInfo, sizeof(poolInfo));
		auto& freeBuffers = m_buffers[hashId];

		GPUOnlyBufferRef buffer = nullptr;
		if (freeBuffers.empty())
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
			std::swap(freeBuffers.front(), freeBuffers.back()); // Use the oldest gay.

			buffer = std::dynamic_pointer_cast<GPUOnlyBuffer>(freeBuffers.back().buffer);
			buffer->rename(name, false);
			freeBuffers.pop_back();

			recentUsedUpdate(freeBuffers);
		}

		return std::make_shared<GPUBufferPool::GPUOnlyPoolBuffer>(buffer, hashId, *this, bSameFrameReuse);
	}



	PoolBufferHostVisible GPUBufferPool::createHostVisible(
		const std::string& name,
		VkBufferUsageFlags usage,
		SizedBuffer data,
		VkBufferCreateFlags flags)
	{
		PoolBufferCreateInfo poolInfo{};
		poolInfo.flags = flags;
		poolInfo.size  = divideRoundingUp(data.size, kBufferSizeAllocRound) * kBufferSizeAllocRound;
		poolInfo.usage = usage;
		poolInfo.vmaCreateFlag = getHostVisibleGPUBufferVMACI().flags;

		const uint64 hashId = cityhash::cityhash64((const char*)&poolInfo, sizeof(poolInfo));
		auto& freeBuffers = m_buffers[hashId];

		HostVisibleGPUBufferRef buffer = nullptr;
		if (freeBuffers.empty())
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
			std::swap(freeBuffers.front(), freeBuffers.back()); // Use the oldest gay.

			buffer = std::dynamic_pointer_cast<HostVisibleGPUBuffer>(freeBuffers.back().buffer);

			buffer->rename(name, false);
			if (data.isValid())
			{
				buffer->copyTo(data.ptr, data.size);
			}

			freeBuffers.pop_back();

			recentUsedUpdate(freeBuffers);
		}

		return std::make_shared<GPUBufferPool::HostVisiblePoolBuffer>(buffer, hashId, *this);
	}

	GPUBufferPool::PoolBuffer::~PoolBuffer()
	{
		FreePoolBuffer freeBuffer;
		freeBuffer.buffer = m_buffer;
		freeBuffer.freeFrame = m_pool.m_frameCounter;

		m_pool.m_buffers[m_hashId].push_back(freeBuffer);

		const auto freeCount = m_pool.m_buffers[m_hashId].size();
		if (freeCount % 1000 == 0)
		{
			LOG_TRACE("Poolbuffer freeCount increment already reach {0}...", freeCount);
		}
	}

	uint32 GPUBufferPool::HostVisiblePoolBuffer::getBindlessIndex() const
	{
		const auto& viewC = m_buffer->requireView(true, false);
		return viewC.storage.get();
	}
}

