#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/bindless.h>
#include <application/application.h>
#include <utils/engine.h>
#include <utils/cityhash.h>

namespace chord::graphics
{
	static bool bGraphicsBufferLifeLogTraceEnable = false;
	static AutoCVarRef<bool> cVarBufferLifeLogTraceEnable(
		"r.graphics.resource.buffer.lifeLogTrace",
		bGraphicsBufferLifeLogTraceEnable,
		"Enable log trace for buffer create/destroy or not.",
		EConsoleVarFlags::ReadOnly
	);

	// Global device size for GPUBuffer.
	static VkDeviceSize sTotalGPUBufferDeviceSize = 0;


	GPUBuffer::GPUBuffer(
		const std::string& name,
		const VkBufferCreateInfo& createInfo,
		const VmaAllocationCreateInfo& vmaCreateInfo)
		: GPUResource(name, 0)
		, m_createInfo(createInfo)
	{

		VmaAllocationCreateInfo copyVMAInfo = vmaCreateInfo;
		copyVMAInfo.pUserData = (void*)getName().c_str();

		checkVkResult(vmaCreateBuffer(getVMA(), &m_createInfo, &vmaCreateInfo, &m_buffer, &m_allocation, &m_vmaAllocationInfo));

		GPUResource::setSize(createInfo.size); // Don't use allocate size.
		rename(name, true);

		sTotalGPUBufferDeviceSize += getSize();
		if (bGraphicsBufferLifeLogTraceEnable)
		{
			LOG_GRAPHICS_TRACE("Create GPUBuffer {0} with size {1} KB.", getName(), float(getSize()) / 1024.0f)
		}
	}

	GPUBuffer::~GPUBuffer()
	{
		// Application releasing state guide us use a update or not in blindess free.
		const bool bAppReleasing = (Application::get().getRuntimePeriod() == ERuntimePeriod::Releasing);

		if (bGraphicsBufferLifeLogTraceEnable)
		{
			LOG_GRAPHICS_TRACE("Destroy GPUBuffer {0} with size {1} KB.", getName(), float(getSize()) / 1024.0f)
		}
		sTotalGPUBufferDeviceSize -= getSize();

		if (m_allocation != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(getVMA(), m_buffer, m_allocation);
			m_allocation = VK_NULL_HANDLE;
		}
	}

	void GPUBuffer::rename(const std::string& name, bool bForce)
	{
		if (setName(name, bForce))
		{
			setResourceName(VK_OBJECT_TYPE_BUFFER, (uint64)m_buffer, getName().c_str());
		}
	}

	uint64 GPUBuffer::getDeviceAddress()
	{
		checkGraphicsMsgf(m_createInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			"Buffer {0} usage must exist 'VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT' if you want to require device addres.", getName());

		if (!m_deviceAddress.isValid())
		{
			VkBufferDeviceAddressInfo bufferDeviceAddressInfo{ };
			bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			bufferDeviceAddressInfo.buffer = m_buffer;

			m_deviceAddress = vkGetBufferDeviceAddress(getDevice(), &bufferDeviceAddressInfo);
		}
		return m_deviceAddress.get();
	}

	BufferView GPUBuffer::requireView(bool bStorage, bool bUniform, VkDeviceSize offset, VkDeviceSize range)
	{
		struct Desc
		{
			VkDeviceSize offset;
			VkDeviceSize range;
		};
		Desc desc { .offset = offset, .range = range };

		auto& view = m_views[cityhash::cityhash64((const char*)(&desc), sizeof(desc))];
		if (bUniform && (!view.uniform.isValid()))
		{
			view.uniform = getBindless().registerUniformBuffer(m_buffer, offset, range);
		}
		if (bStorage && (!view.storage.isValid()))
		{
			view.storage = getBindless().registerStorageBuffer(m_buffer, offset, range);
		}

		return view;
	}

	VkBufferMemoryBarrier2 GPUBuffer::updateBarrier(GPUSyncBarrierMasks newMask)
	{
		auto oldQueueFamily = (m_syncBarrier.queueFamilyIndex == VK_QUEUE_FAMILY_IGNORED)
			? newMask.queueFamilyIndex
			: m_syncBarrier.queueFamilyIndex;

		// Need transfer ownership.
		VkBufferMemoryBarrier2 barrier = helper::bufferBarrier(m_buffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, m_syncBarrier.accesMask, oldQueueFamily,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, newMask.accesMask, newMask.queueFamilyIndex, 0, VK_WHOLE_SIZE);

		m_syncBarrier = newMask;
		return barrier;
	}

	GPUOnlyBuffer::GPUOnlyBuffer(
		const std::string& name,
		const VkBufferCreateInfo& createInfo)
		: GPUBuffer(name, createInfo, getGPUOnlyBufferVMACI())
	{

	}

	GPUOnlyBuffer::~GPUOnlyBuffer()
	{
		// Application releasing state guide us use a update or not in blindess free.
		const bool bAppReleasing = (Application::get().getRuntimePeriod() == ERuntimePeriod::Releasing);
		GPUBufferRef fallbackSSBO = bAppReleasing ? nullptr : getContext().getDummySSBO();

		// Destroy cached image views.
		for (auto& pair : m_views)
		{
			auto& view = pair.second;
			if (view.storage.isValid())
			{
				getBindless().freeStorageBuffer(view.storage, fallbackSSBO);
			}

			if (view.uniform.isValid())
			{
				getBindless().freeUniformBuffer(view.uniform, fallbackSSBO);
			}
		}
	}

	//

	HostVisibleGPUBuffer::HostVisibleGPUBuffer(
		const std::string& name,
		const VkBufferCreateInfo& createInfo,
		SizedBuffer data)
		: GPUBuffer(name, createInfo, getHostVisibleGPUBufferVMACI())
	{
		if (data.isValid())
		{
			copyTo(data.ptr, data.size);
		}
	}

	HostVisibleGPUBuffer::~HostVisibleGPUBuffer()
	{
		// Unmap buffer before release.
		unmap();
	}

	void HostVisibleGPUBuffer::map(VkDeviceSize size)
	{
		if (m_mapped == nullptr)
		{
			checkVkResult(vmaMapMemory(getVMA(), m_allocation, &m_mapped));
		}
	}

	void HostVisibleGPUBuffer::unmap()
	{
		if (m_mapped != nullptr)
		{
			vmaUnmapMemory(getVMA(), m_allocation);
			m_mapped = nullptr;
		}
	}

	void HostVisibleGPUBuffer::flush(VkDeviceSize size, VkDeviceSize offset)
	{
		checkVkResult(vmaFlushAllocation(getVMA(), m_allocation, offset, size));
	}

	void HostVisibleGPUBuffer::invalidate(VkDeviceSize size, VkDeviceSize offset)
	{
		checkVkResult(vmaInvalidateAllocation(getVMA(), m_allocation, offset, size));
	}

	void HostVisibleGPUBuffer::copyTo(const void* data, VkDeviceSize size)
	{
		checkGraphicsMsgf(!m_mapped, "Buffer already mapped, don't use this function, just memcpy is fine.");

		// Don't overflow.
		checkGraphics(size <= getSize());

		map(size);
		{
			// Copy then flush.
			memcpy(m_mapped, data, size);
			flush(size);
		}
		unmap();
	}
}