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

		auto assetPtr = std::dynamic_pointer_cast<GLTFAsset>(shared_from_this());
		auto newGPUPrimitives = std::make_shared<GPUGLTFPrimitiveAsset>(m_saveInfo.getName().u8(), assetPtr);
		const size_t totalUsedSize = m_gltfBinSize; // Primitive bin size.

		getContext().getAsyncUploader().addTask(m_gltfBinSize,
			[newGPUPrimitives, assetPtr, totalUsedSize](uint32 offset, uint32 queueFamily, void* mapped, VkCommandBuffer cmd, VkBuffer buffer)
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
					vkCmdCopyBuffer(cmd, buffer, *comp.buffer, 1, &regionCopy);

					sizeAccumulate += regionCopy.size;
				};

				{
					auto bufferFlagBasic = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
					VmaAllocationCreateFlags bufferFlagVMA = {};
					if (getContext().isRaytraceSupport())
					{
						// Raytracing accelerate struct, random shader fetch by address.
						bufferFlagBasic |=
							VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
							VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
						bufferFlagVMA = {};
					}
					static_assert(sizeof(math::vec3) == sizeof(float) * 3);

					newGPUPrimitives->indices = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_indices"),
						bufferFlagBasic | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.indices[0]),
						(uint32)gltfBin.primitiveData.indices.size());
					copyBuffer(*newGPUPrimitives->indices, gltfBin.primitiveData.indices.data());

					newGPUPrimitives->meshlet = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_meshlet"),
						bufferFlagBasic,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.meshlets[0]),
						(uint32)gltfBin.primitiveData.meshlets.size());
					copyBuffer(*newGPUPrimitives->meshlet, gltfBin.primitiveData.meshlets.data());

					newGPUPrimitives->positions = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_positions"),
						bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.positions[0]),
						(uint32)gltfBin.primitiveData.positions.size());
					copyBuffer(*newGPUPrimitives->positions, gltfBin.primitiveData.positions.data());

					newGPUPrimitives->normals = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_normals"),
						bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.normals[0]),
						(uint32)gltfBin.primitiveData.normals.size());
					copyBuffer(*newGPUPrimitives->normals, gltfBin.primitiveData.normals.data());

					newGPUPrimitives->uv0s = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_uv0s"),
						bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.texcoords0[0]),
						(uint32)gltfBin.primitiveData.texcoords0.size());
					copyBuffer(*newGPUPrimitives->uv0s, gltfBin.primitiveData.texcoords0.data());

					newGPUPrimitives->tangents = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_tangents"),
						bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.tangents[0]),
						(uint32)gltfBin.primitiveData.tangents.size());
					copyBuffer(*newGPUPrimitives->tangents, gltfBin.primitiveData.tangents.data());

					if (!gltfBin.primitiveData.smoothNormals.empty())
					{
						newGPUPrimitives->smoothNormals = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
							getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_smoothNormals"),
							bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
							bufferFlagVMA,
							(uint32)sizeof(gltfBin.primitiveData.smoothNormals[0]),
							(uint32)gltfBin.primitiveData.smoothNormals.size());

						copyBuffer(*newGPUPrimitives->smoothNormals, gltfBin.primitiveData.smoothNormals.data());
					}

					if (!gltfBin.primitiveData.colors0.empty())
					{
						newGPUPrimitives->colors = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
							getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_colors0"),
							bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
							bufferFlagVMA,
							(uint32)sizeof(gltfBin.primitiveData.colors0[0]),
							(uint32)gltfBin.primitiveData.colors0.size());

						copyBuffer(*newGPUPrimitives->colors, gltfBin.primitiveData.colors0.data());
					}

					if (!gltfBin.primitiveData.texcoords1.empty())
					{
						newGPUPrimitives->uv1s = std::make_unique<GPUGLTFPrimitiveAsset::ComponentBuffer>(
							getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_texcoords1"),
							bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
							bufferFlagVMA,
							(uint32)sizeof(gltfBin.primitiveData.texcoords1[0]),
							(uint32)gltfBin.primitiveData.texcoords1.size());

						copyBuffer(*newGPUPrimitives->uv1s, gltfBin.primitiveData.texcoords1.data());
					}
				}

				checkMsgf(totalUsedSize == sizeAccumulate, "Mesh primitive data size un-match!");
			},
			[newGPUPrimitives]() // Finish loading.
			{
				newGPUPrimitives->setLoadingState(false);
				newGPUPrimitives->updateGPUScene();
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
