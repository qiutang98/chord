#pragma once

#include <graphics/resource.h>

namespace chord::graphics
{
	struct PoolTextureCreateInfo
	{
		VkFormat              format;
		VkExtent3D            extent;
		VkImageUsageFlags     usage;
		VkImageType           imageType;

		VkImageCreateFlags    flags       = 0;
		uint32                mipLevels   = 1;
		uint32                arrayLayers = 1;
		VkSampleCountFlagBits samples     = VK_SAMPLE_COUNT_1_BIT;
		VkImageTiling         tiling      = VK_IMAGE_TILING_OPTIMAL;
	};

	class GPUTexturePool : NonCopyable
	{
	public:
		class PoolTexture : public IResource
		{
			friend GPUTexturePool;
		public:
			explicit PoolTexture(GPUTextureRef inTexture, uint64 hashId, GPUTexturePool& pool)
				: m_texture(inTexture)
				, m_hashId(hashId)
				, m_pool(pool)
			{
			}

			operator const GPUTexture&() const { return *m_texture; }
			operator GPUTexture&() { return *m_texture; }

			virtual ~PoolTexture()
			{
				// We should ensure pool image life shorter than pool.
				m_pool.m_rendertargets[m_hashId].push(m_texture);
			}

		protected:
			const uint64 m_hashId;
			GPUTexturePool& m_pool;
			GPUTextureRef m_texture = nullptr;
		};

		// Create pool texture by create info.
		std::shared_ptr<PoolTexture> create(
			const std::string& name, 
			const PoolTextureCreateInfo& createInfo);

		// Common 2d pool texture create.
		std::shared_ptr<PoolTexture> create(
			const std::string& name,
			uint32 width,
			uint32 height,
			VkFormat format,
			VkImageUsageFlags usage);

		// Pool cube texture create.
		std::shared_ptr<PoolTexture> createCube(
			const std::string& name,
			uint32 width,
			uint32 height,
			VkFormat format,
			VkImageUsageFlags usage);

	private:
		friend PoolTexture;

		// Free render targets.
		std::map<uint64, std::stack<GPUTextureRef>> m_rendertargets;

	};
	using GPUTexturePoolRef = std::shared_ptr<GPUTexturePool>;
	using PoolTextureRef = std::shared_ptr<GPUTexturePool::PoolTexture>;
}