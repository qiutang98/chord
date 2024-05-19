#include <asset/gltf/gltf.h>
#include <asset/gltf/gltf_helper.h>
#include <asset/serialize.h>

namespace chord
{
	using namespace graphics;

	const AssetTypeMeta GLTFAsset::kAssetTypeMeta = GLTFAsset::createTypeMeta();
	const AssetTypeMeta GLTFMaterialAsset::kAssetTypeMeta = GLTFMaterialAsset::createTypeMeta();

	GLTFAsset::GLTFAsset(const AssetSaveInfo& saveInfo)
		: IAsset(saveInfo)
	{
	}

	bool GLTFAsset::isGPUPrimitivesStreamingReady() const
	{
		if (auto cache = m_gpuPrimitives.lock())
		{
			return cache->isReady();
		}

		return false;
	}

	GPUGLTFPrimitiveAssetRef GLTFAsset::getGPUPrimitives()
	{
		if (auto cache = m_gpuPrimitives.lock())
		{
			return cache;
		}

		auto assetPtr = shared_from_this();
		auto newGPUPrimitives = std::make_shared<GPUGLTFPrimitiveAsset>(m_saveInfo.getName().u8());
		const size_t totalUsedSize = m_gltfBinSize; // Primitive bin size.

		getContext().getAsyncUploader().addTask(m_gltfBinSize,
			[newGPUPrimitives, assetPtr](uint32 offset, uint32 queueFamily, void* mapped, VkCommandBuffer cmd, VkBuffer buffer)
			{
				GLTFBinary gltfBin{};
				if (!std::filesystem::exists(assetPtr->getBinPath()))
				{
					checkEntry();
				}
				else
				{
					LOG_TRACE("Found bin for asset {} cache in disk so just load.",
						utf8::utf16to8(assetPtr->getSaveInfo().relativeAssetStorePath().u16string()));
					loadAsset(gltfBin, assetPtr->getBinPath());
				}

				size_t sizeAccumulate = 0;
				auto copyBuffer = [&](const GPUGLTFPrimitiveAsset::ComponentBuffer& comp, const void* data)
				{
					VkBufferCopy regionCopy{ };

					regionCopy.size = comp.stripe * comp.elementNum;
					regionCopy.srcOffset = offset + sizeAccumulate;
					regionCopy.dstOffset = 0;

					memcpy((void*)((char*)mapped + sizeAccumulate), data, regionCopy.size);

					vkCmdCopyBuffer(cmd, buffer, comp.buffer->getVkBuffer(), 1, &regionCopy);

					sizeAccumulate += regionCopy.size;
				};

			},
			[newGPUPrimitives]() // Finish loading.
			{
				newGPUPrimitives->setLoadingState(false);
			});


		m_gpuPrimitives = newGPUPrimitives;
		return newGPUPrimitives;
	}

	void GLTFAsset::onPostConstruct()
	{

	}

	bool GLTFAsset::onSave()
	{
		std::shared_ptr<IAsset> asset = ptr<GLTFAsset>();
		return saveAsset(asset, ECompressionMode::Lz4, m_saveInfo.path(), false);
	}

	void GLTFAsset::onUnload()
	{

	}

	GLTFMaterialAsset::GLTFMaterialAsset(const AssetSaveInfo& saveInfo)
		: IAsset(saveInfo)
	{
	}

	void GLTFMaterialAsset::onPostConstruct()
	{

	}

	bool GLTFMaterialAsset::onSave()
	{
		std::shared_ptr<IAsset> asset = ptr<GLTFMaterialAsset>();
		return saveAsset(asset, ECompressionMode::Lz4, m_saveInfo.path(), false);
	}

	void GLTFMaterialAsset::onUnload()
	{

	}
}
