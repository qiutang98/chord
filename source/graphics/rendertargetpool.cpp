#include <graphics/rendertargetpool.h>
#include <graphics/graphics.h>
#include <graphics/resource.h>
#include <utils/cityhash.h>

namespace chord::graphics
{
	PoolTextureRef GPUTexturePool::createCube(
		const std::string& name,
		uint32 width,
		uint32 height,
		VkFormat format,
		VkImageUsageFlags usage)
	{
		PoolTextureCreateInfo ci { };
		ci.format      = format;
		ci.extent      = { .width = width, .height = height, .depth = 1 };
		ci.usage       = usage;
		ci.imageType   = VK_IMAGE_TYPE_2D;
		ci.arrayLayers = 6;
		ci.flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		return GPUTexturePool::create(name, ci);
	}

	PoolTextureRef GPUTexturePool::create(
		const std::string& name,
		uint32 width,
		uint32 height,
		VkFormat format,
		VkImageUsageFlags usage)
	{
		PoolTextureCreateInfo ci { };
		ci.format    = format;
		ci.extent    = { .width = width, .height = height, .depth = 1 };
		ci.usage     = usage;
		ci.imageType = VK_IMAGE_TYPE_2D;

		return GPUTexturePool::create(name, ci);
	}

	PoolTextureRef GPUTexturePool::create(const std::string& name, const PoolTextureCreateInfo& createInfo)
	{
		const uint64 hashId = cityhash::cityhash64((const char*)&createInfo, sizeof(createInfo));
		auto& list = m_rendertargets[hashId];

		// Render target empty.
		GPUTextureRef texture = nullptr;
		if (list.empty())
		{
			VkImageCreateInfo ci { };
			ci.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ci.flags       = createInfo.flags;
			ci.imageType   = createInfo.imageType;
			ci.format      = createInfo.format;
			ci.extent      = createInfo.extent;
			ci.mipLevels   = createInfo.mipLevels;
			ci.arrayLayers = createInfo.arrayLayers;
			ci.samples     = createInfo.samples;
			ci.tiling      = createInfo.tiling;
			ci.usage       = createInfo.usage;

			// Fixed config.
			ci.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
			ci.queueFamilyIndexCount = 0;
			ci.pQueueFamilyIndices   = nullptr;
			ci.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED;

			VmaAllocationCreateInfo vmaCI { };
			vmaCI.usage = VMA_MEMORY_USAGE_AUTO;

			texture = std::make_shared<GPUTexture>(name, ci, vmaCI);
		}
		else
		{
			texture = list.top();
			texture->rename(name);
			list.pop();
		}

		return std::make_shared<GPUTexturePool::PoolTexture>(texture, hashId, *this);
	}

}

