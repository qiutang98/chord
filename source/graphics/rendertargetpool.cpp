#include <graphics/rendertargetpool.h>
#include <graphics/graphics.h>
#include <graphics/resource.h>
#include <utils/cityhash.h>
#include <application/application.h>

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

	GPUTexturePool::~GPUTexturePool()
	{
		m_rendertargets.clear();
	}

	void GPUTexturePool::garbageCollected(const ApplicationTickData& tickData)
	{
		// Update inner counter.
		m_frameCounter = tickData.tickCount;

		// Clear garbages.
		std::vector<uint64> emptyKeys;
		for (auto& texturesPair : m_rendertargets)
		{
			const auto& key = texturesPair.first;
			auto& textures = texturesPair.second;

			if (!textures.empty())
			{
				textures.erase(std::remove_if(textures.begin(), textures.end(), [&](const auto& t)
				{
					if (m_frameCounter - t.freeFrame > m_freeFrameCount)
					{
						LOG_TRACE("Remove texture {2}: ({0}x{1}).", t.texture->getExtent().width, t.texture->getExtent().height, t.texture->getName());
					}
					return m_frameCounter - t.freeFrame > m_freeFrameCount;
				}), textures.end());
			}


			if (textures.empty())
			{
				emptyKeys.push_back(key);
			}
		}

		for (const auto& key : emptyKeys)
		{
			m_rendertargets.erase(key);
		}
	}

	PoolTextureRef GPUTexturePool::create(const std::string& name, const PoolTextureCreateInfo& createInfo)
	{
		const uint64 hashId = cityhash::cityhash64((const char*)&createInfo, sizeof(createInfo));
		auto& freeTextures = m_rendertargets[hashId];

		// Render target empty.
		GPUTextureRef texture = nullptr;
		if (freeTextures.empty())
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
			std::swap(freeTextures.front(), freeTextures.back()); // Use the oldest gay.

			texture = freeTextures.back().texture;
			texture->rename(name, false);
			freeTextures.pop_back();

			// Current type texture use this frame, may exist a lot of chance reuse later.
			// Step increment free frame.
			if (!freeTextures.empty())
			{
				for (auto& texture : freeTextures)
				{
					texture.freeFrame = math::max(texture.freeFrame, getFrameCounter() - m_freeFrameCount + 1);
				}
			}
		}

		return std::make_shared<GPUTexturePool::PoolTexture>(texture, hashId, *this);
	}

	GPUTexturePool::PoolTexture::~PoolTexture()
	{
		FreePoolTexture poolTexture;
		poolTexture.freeFrame = m_pool.m_frameCounter;
		poolTexture.texture = m_texture;

		// We should ensure pool image life shorter than pool.
		m_pool.m_rendertargets[m_hashId].push_back(poolTexture);
	}

}

