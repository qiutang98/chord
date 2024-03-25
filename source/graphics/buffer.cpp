#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/bindless.h>
#include <application/application.h>
#include <utils/engine.h>

namespace chord::graphics
{
	static bool bGraphicsBufferLifeLogTraceEnable = true;
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
		const VmaAllocationCreateInfo& vmaCreateInfo,
		SizedBuffer data)
	: GPUResource(name, 0)
	, m_createInfo(createInfo)
	{
		VmaAllocationCreateInfo copyVMAInfo = vmaCreateInfo;
		copyVMAInfo.pUserData = (void*)getName().c_str();

		checkVkResult(vmaCreateBuffer(getVMA(), &m_createInfo, &vmaCreateInfo, &m_buffer, &m_allocation, &m_vmaAllocationInfo));

		GPUResource::setSize(m_vmaAllocationInfo.size);
		rename(name);

		sTotalGPUBufferDeviceSize += getSize();
		if (bGraphicsBufferLifeLogTraceEnable)
		{
			LOG_GRAPHICS_TRACE("Create GPUBuffer {0} with size {1} KB.", getName(), float(getSize()) / 1024.0f)
		}

		if (data.isValid())
		{
			copyTo(data.ptr, data.size);
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

		// Unmap buffer before release.
		unmap();

		if (m_allocation != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(getVMA(), m_buffer, m_allocation);
			m_allocation = VK_NULL_HANDLE;
		}
	}

	void GPUBuffer::rename(const std::string& name)
	{
		Super::rename(name);
		setResourceName(VK_OBJECT_TYPE_BUFFER, (uint64)m_buffer, name.c_str());
	}

	uint64 GPUBuffer::getDeviceAddress()
	{
		checkGraphicsMsgf(m_createInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
			"Buffer {0} usage must exist 'VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT' if you want to require device addres.", getName());

		if (!m_deviceAddress.isValid())
		{
			VkBufferDeviceAddressInfo bufferDeviceAddressInfo { };
			bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
			bufferDeviceAddressInfo.buffer = m_buffer;

			m_deviceAddress = vkGetBufferDeviceAddress(getDevice(), &bufferDeviceAddressInfo);
		}
		return m_deviceAddress.get();
	}

	void GPUBuffer::map(VkDeviceSize size)
	{
		if (m_mapped == nullptr)
		{
			checkVkResult(vmaMapMemory(getVMA(), m_allocation, &m_mapped));
		}
	}

	void GPUBuffer::unmap()
	{
		if (m_mapped != nullptr)
		{
			vmaUnmapMemory(getVMA(), m_allocation);
			m_mapped = nullptr;
		}
	}

	void GPUBuffer::flush(VkDeviceSize size, VkDeviceSize offset)
	{
		checkVkResult(vmaFlushAllocation(getVMA(), m_allocation, offset, size));
	}

	void GPUBuffer::invalidate(VkDeviceSize size, VkDeviceSize offset)
	{
		checkVkResult(vmaInvalidateAllocation(getVMA(), m_allocation, offset, size));
	}

	void GPUBuffer::copyTo(const void* data, VkDeviceSize size)
	{
		checkGraphicsMsgf(!m_mapped, "Buffer already mapped, don't use this function, just memcpy is fine.");

		// Don't overflow.
		checkGraphics(size <= getSize());

		map(size);
		
		// Copy then flush.
		memcpy(m_mapped, data, size);
		flush(size, 0);

		unmap();
	}
}