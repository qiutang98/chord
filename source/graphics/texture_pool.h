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
		class PoolTexture : public IPoolResource
		{
			friend GPUTexturePool;
		public:
			explicit PoolTexture(GPUTextureRef inTexture, uint64 hashId, GPUTexturePool& pool, bool bSameFrameReuse)
				: IPoolResource(bSameFrameReuse)
				, m_texture(inTexture)
				, m_hashId(hashId)
				, m_pool(pool)
			{
			}

			const GPUTexture& get() const { return *m_texture; }
			GPUTexture& get() { return *m_texture; }

			virtual ~PoolTexture();

			virtual GPUResourceRef getGPUResourceRef() const override
			{
				return m_texture;
			}

			GPUTextureRef getGPUTextureRef() const { return m_texture; }

		protected:
			const uint64 m_hashId;
			GPUTexturePool& m_pool;
			GPUTextureRef m_texture = nullptr;
		};

		explicit GPUTexturePool(uint64 freeFrameCount)
			: m_freeFrameCount(freeFrameCount)
		{

		}

		virtual ~GPUTexturePool();

		void garbageCollected(const ApplicationTickData& tickData);

		// Create pool texture by create info.
		std::shared_ptr<PoolTexture> create(
			const std::string& name, 
			const PoolTextureCreateInfo& createInfo,
			bool bSameFrameReuse = true);

		// Common 2d pool texture create.
		std::shared_ptr<PoolTexture> create(
			const std::string& name,
			uint32 width,
			uint32 height,
			VkFormat format,
			VkImageUsageFlags usage,
			bool bSameFrameReuse = true);

		// Pool cube texture create.
		std::shared_ptr<PoolTexture> createCube(
			const std::string& name,
			uint32 width,
			uint32 height,
			VkFormat format,
			VkImageUsageFlags usage,
			bool bSameFrameReuse = true);

	private:
		friend PoolTexture;

		// Frame count ready to release.
		const uint64 m_freeFrameCount;

		// Pool frame counter.
		uint64 m_frameCounter = 0;

		// Free render targets.
		struct FreePoolTexture
		{
			GPUTextureRef texture;
			uint64 freeFrame = 0;
		};
		std::map<uint64, std::vector<FreePoolTexture>> m_rendertargets;
	};
	using GPUTexturePoolRef = std::shared_ptr<GPUTexturePool>;
	using PoolTextureRef = std::shared_ptr<GPUTexturePool::PoolTexture>;
}