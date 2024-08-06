#include <asset/texture/texture.h>
#include <asset/serialize.h>
#include <graphics/helper.h>

namespace chord
{
	using namespace graphics;

	const AssetTypeMeta TextureAsset::kAssetTypeMeta = TextureAsset::createTypeMeta();

	TextureAsset::TextureAsset(const AssetSaveInfo& saveInfo)
		: IAsset(saveInfo)
	{

	}

	void TextureAsset::onPostConstruct()
	{

	}

	bool TextureAsset::onSave()
	{
		std::shared_ptr<IAsset> asset = ptr<TextureAsset>();
		return saveAsset(asset, ECompressionMode::Lz4, m_saveInfo.path(), false);
	}

	void TextureAsset::onUnload()
	{

	}

	TextureAssetRef tryLoadTextureAsset(const std::filesystem::path& path, bool bThreadSafe)
	{
		return Application::get().getAssetManager().getOrLoadAsset<TextureAsset>(path, bThreadSafe);
	}

	bool TextureAsset::isGPUTextureStreamingReady() const
	{
		if (auto cache = m_gpuTexture.lock())
		{
			return cache->isReady();
		}
		return false;
	}

	GPUTextureAssetRef TextureAsset::getGPUTexture(std::function<void(graphics::GPUTextureAssetRef)>&& afterLoadingCallback)
	{
		if (auto cache = m_gpuTexture.lock())
		{
			return cache;
		}

		VkImageCreateInfo ci { };
		ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ci.flags         = {};
		ci.imageType     = (m_dimension.z == 1) ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_3D;
		ci.format        = m_format;
		ci.extent.width  = m_dimension.x;
		ci.extent.height = m_dimension.y;
		ci.extent.depth  = m_dimension.z;
		ci.arrayLayers   = 1;
		ci.mipLevels     = m_mipmapCount;
		ci.samples       = VK_SAMPLE_COUNT_1_BIT;
		ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
		ci.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
		ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		auto uploadVMACI = helper::buildVMAUploadImageAllocationCI();

		auto assetPtr = std::dynamic_pointer_cast<TextureAsset>(shared_from_this());
		auto newGPUTexture = std::make_shared<GPUTextureAsset>(
			getContext().getBuiltinTextures().white.get(),
			m_saveInfo.getName().u8(), 
			ci,
			uploadVMACI);

		getContext().getAsyncUploader().addTask(newGPUTexture->getSize(),
			[newGPUTexture, assetPtr](uint32 offset, uint32 queueFamily, void* mapped, VkCommandBuffer cmd, VkBuffer buffer)
			{
				auto texture = newGPUTexture->getOwnHandle();
				TextureAssetBin textureBin{};
				if (!std::filesystem::exists(assetPtr->getBinPath()))
				{
					checkEntry();
				}
				else
				{
					LOG_TRACE("Found bin for asset {} cache in disk so just load.",
						utf8::utf16to8(assetPtr->getSaveInfo().relativeAssetStorePath().u16string()));
					loadAsset(textureBin, assetPtr->getBinPath());
				}

				VkImageSubresourceRange rangeAllMips = helper::buildBasicImageSubresource();
				rangeAllMips.levelCount = assetPtr->getMipmapCount();

				newGPUTexture->prepareToUpload(cmd, queueFamily, rangeAllMips);

				uint32 bufferOffset = 0;
				uint32 bufferSize   = 0;

				VkBufferImageCopy region{};
				region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				region.imageSubresource.baseArrayLayer = 0;
				region.imageSubresource.layerCount = 1;
				region.imageOffset = { 0, 0, 0 };
				region.bufferRowLength = 0;
				region.bufferImageHeight = 0;

				std::vector<VkBufferImageCopy> copyRegions{};
				const auto& mipmapDatas = textureBin.mipmapDatas;
				for (uint32 level = 0; level < assetPtr->getMipmapCount(); level++)
				{
					const auto& currentMip = mipmapDatas.at(level);
					const uint32 currentMipSize = (uint32)currentMip.size();

					uint32 mipWidth  = std::max<uint32>(assetPtr->getDimension().x >> level, 1);
					uint32 mipHeight = std::max<uint32>(assetPtr->getDimension().y >> level, 1);

					memcpy((void*)((char*)mapped + bufferOffset), currentMip.data(), currentMipSize);

					region.bufferOffset = offset + bufferOffset;
					region.imageSubresource.mipLevel = level;
					region.imageExtent = { mipWidth, mipHeight, 1 };

					copyRegions.push_back(region);

					bufferOffset += currentMipSize;
					bufferSize   += currentMipSize;
				}
				checkMsgf(newGPUTexture->getSize() >= bufferSize, "Upload size must bigger than buffer size!");

				// Upload command.
				vkCmdCopyBufferToImage(cmd, buffer, *texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32)copyRegions.size(), copyRegions.data());

				// Finish upload we change to graphics family.
				newGPUTexture->finishUpload(cmd, getContext().getQueuesInfo().graphicsFamily.get(), rangeAllMips);
			},
			[newGPUTexture, afterLoadingCallback]()
			{
				newGPUTexture->setLoadingState(false);

				if (afterLoadingCallback)
				{
					afterLoadingCallback(newGPUTexture);
				}
			});

		m_gpuTexture = newGPUTexture;
		return newGPUTexture;
	}
}