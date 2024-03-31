#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/bindless.h>

namespace chord::graphics
{
	GPUSamplerManager::GPUSamplerManager()
	{
		// Default allocate.
		pointClampEdge();
		pointClampBorder0000();
		pointClampBorder1111();
		pointRepeat();
		linearClampEdgeMipPoint();
		linearClampBorder0000MipPoint();
		linearClampBorder1111MipPoint();
		linearRepeatMipPoint();
		linearClampEdge();
		linearRepeat();
	}

	GPUSamplerManager::~GPUSamplerManager()
	{
		for (auto& pair : m_samplers)
		{
			vkDestroySampler(getDevice(), pair.second.handle, getAllocationCallbacks());
		}
	}

	GPUSamplerManager::Sampler GPUSamplerManager::getSampler(SamplerCreateInfo info)
	{
		// Add or load sampler.
		auto& sampler = m_samplers[crc::crc32(info)];

		// Create new sampler if not exist yet.
		if (!sampler.index.isValid())
		{
			checkGraphics(sampler.handle == VK_NULL_HANDLE);

			VkSamplerCreateInfo ci{};

			ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			ci.flags = info.flags;
			ci.magFilter = info.magFilter;
			ci.minFilter = info.minFilter;
			ci.mipmapMode = info.mipmapMode;
			ci.addressModeU = info.addressModeU;
			ci.addressModeV = info.addressModeV;
			ci.addressModeW = info.addressModeW;
			ci.mipLodBias = info.mipLodBias;
			ci.anisotropyEnable = info.anisotropyEnable;
			ci.maxAnisotropy = info.maxAnisotropy;
			ci.compareEnable = info.compareEnable;
			ci.compareOp = info.compareOp;
			ci.minLod = info.minLod;
			ci.maxLod = info.maxLod;
			ci.borderColor = info.borderColor;
			ci.unnormalizedCoordinates = info.unnormalizedCoordinates;

			checkVkResult(vkCreateSampler(getDevice(), &ci, getAllocationCallbacks(), &sampler.handle));

			// Register in bindless.
			sampler.index = getBindless().registerSampler(sampler.handle);
		}

		return sampler;
	}

	static inline SamplerCreateInfo buildBasic()
	{
		SamplerCreateInfo info { };
		info.magFilter  = VK_FILTER_NEAREST;
		info.minFilter  = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		info.minLod     = -10000.0f;
		info.maxLod     = 10000.0f;
		info.mipLodBias = 0.0f;

		info.anisotropyEnable        = VK_FALSE;
		info.compareEnable           = VK_FALSE;
		info.unnormalizedCoordinates = VK_FALSE;

		return info;
	}

	GPUSamplerManager::Sampler GPUSamplerManager::pointClampEdge()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter = VK_FILTER_NEAREST;
		info.minFilter = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::pointClampBorder0000()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter  = VK_FILTER_NEAREST;
		info.minFilter  = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

		info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::pointClampBorder1111()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter  = VK_FILTER_NEAREST;
		info.minFilter  = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::pointRepeat()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter  = VK_FILTER_NEAREST;
		info.minFilter  = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::linearClampEdgeMipPoint()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;

		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::linearClampBorder0000MipPoint()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter  = VK_FILTER_LINEAR;
		info.minFilter  = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

		info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::linearClampBorder1111MipPoint()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::linearRepeatMipPoint()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::linearClampEdge()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter  = VK_FILTER_LINEAR;
		info.minFilter  = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		return getSampler(info);
	}

	GPUSamplerManager::Sampler GPUSamplerManager::linearRepeat()
	{
		SamplerCreateInfo info = buildBasic();

		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		return getSampler(info);
	}
}