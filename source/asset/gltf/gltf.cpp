#include <asset/gltf/gltf.h>
#include <asset/gltf/gltf_helper.h>
#include <asset/serialize.h>
#include <renderer/gpu_scene.h>

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
				auto copyBuffer = [&](const ComponentBuffer& comp, const void* data)
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

					newGPUPrimitives->indices = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_indices"),
						bufferFlagBasic | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.indices[0]),
						(uint32)gltfBin.primitiveData.indices.size());
					copyBuffer(*newGPUPrimitives->indices, gltfBin.primitiveData.indices.data());

					newGPUPrimitives->meshlet = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_meshlet"),
						bufferFlagBasic,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.meshlets[0]),
						(uint32)gltfBin.primitiveData.meshlets.size());
					copyBuffer(*newGPUPrimitives->meshlet, gltfBin.primitiveData.meshlets.data());

					newGPUPrimitives->positions = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_positions"),
						bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.positions[0]),
						(uint32)gltfBin.primitiveData.positions.size());
					copyBuffer(*newGPUPrimitives->positions, gltfBin.primitiveData.positions.data());

					newGPUPrimitives->normals = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_normals"),
						bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.normals[0]),
						(uint32)gltfBin.primitiveData.normals.size());
					copyBuffer(*newGPUPrimitives->normals, gltfBin.primitiveData.normals.data());

					newGPUPrimitives->uv0s = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_uv0s"),
						bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.texcoords0[0]),
						(uint32)gltfBin.primitiveData.texcoords0.size());
					copyBuffer(*newGPUPrimitives->uv0s, gltfBin.primitiveData.texcoords0.data());

					newGPUPrimitives->tangents = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_tangents"),
						bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.tangents[0]),
						(uint32)gltfBin.primitiveData.tangents.size());
					copyBuffer(*newGPUPrimitives->tangents, gltfBin.primitiveData.tangents.data());

					if (!gltfBin.primitiveData.smoothNormals.empty())
					{
						newGPUPrimitives->smoothNormals = std::make_unique<ComponentBuffer>(
							getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_smoothNormals"),
							bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
							bufferFlagVMA,
							(uint32)sizeof(gltfBin.primitiveData.smoothNormals[0]),
							(uint32)gltfBin.primitiveData.smoothNormals.size());

						copyBuffer(*newGPUPrimitives->smoothNormals, gltfBin.primitiveData.smoothNormals.data());
					}

					if (!gltfBin.primitiveData.colors0.empty())
					{
						newGPUPrimitives->colors = std::make_unique<ComponentBuffer>(
							getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_colors0"),
							bufferFlagBasic | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
							bufferFlagVMA,
							(uint32)sizeof(gltfBin.primitiveData.colors0[0]),
							(uint32)gltfBin.primitiveData.colors0.size());

						copyBuffer(*newGPUPrimitives->colors, gltfBin.primitiveData.colors0.data());
					}

					if (!gltfBin.primitiveData.texcoords1.empty())
					{
						newGPUPrimitives->uv1s = std::make_unique<ComponentBuffer>(
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

	GLTFMaterialProxyRef GLTFMaterialAsset::getProxy()
	{
		if (auto proxy = m_proxy.lock())
		{
			return proxy;
		}

		GLTFMaterialAssetRef assetPtr = ptr<GLTFMaterialAsset>();
		auto proxy = std::make_shared<GLTFMaterialProxy>(assetPtr);
		GLTFMaterialProxy::init(proxy);

		m_proxy = proxy;
		return proxy;
	}



	GLTFMaterialProxy::GLTFMaterialProxy(GLTFMaterialAssetRef material)
		: reference(material)
		, m_proxyId(requireUniqueId())
	{

	}

	GLTFMaterialProxy::~GLTFMaterialProxy()
	{
		freeGPUScene();
	}

	void GLTFMaterialProxy::init(std::shared_ptr<GLTFMaterialProxy> proxy)
	{
		auto fallbackImage = getContext().getBuiltinTextures().white;
		std::weak_ptr<GLTFMaterialProxy> weakPtr = proxy;

		auto proxyLoadTexture = [&](const auto& materialTex, auto& tex)
		{
			if (materialTex.isValid())
			{
				auto asset = tryLoadTextureAsset(materialTex.image);
				tex = asset->getGPUTexture([weakPtr](GPUTextureAssetRef texture)
				{
					// When loading ready, require update gpu scene.
					if (auto ptr = weakPtr.lock())
					{
						ptr->updateGPUScene(false);
					}
				});
			}
			else
			{
				tex = fallbackImage;
			}
		};

		// Load all needed texture.
		proxyLoadTexture(proxy->reference->baseColorTexture, proxy->baseColorTexture);
		proxyLoadTexture(proxy->reference->metallicRoughnessTexture, proxy->metallicRoughnessTexture);
		proxyLoadTexture(proxy->reference->emissiveTexture, proxy->emissiveTexture);
		proxyLoadTexture(proxy->reference->normalTexture, proxy->normalTexture);

		// Require gpu scene space.
		proxy->m_gpuSceneGLTFMaterialAssetId = Application::get().getGPUScene().getGLTFMaterialPool().requireId(proxy->m_proxyId);
		proxy->updateGPUScene(true);
	}

	void GLTFMaterialProxy::updateGPUScene(bool bForceUpload)
	{
		const bool bAllTextureFinish = baseColorTexture->isReady() && emissiveTexture->isReady() && normalTexture->isReady() && metallicRoughnessTexture->isReady();

		if (!bForceUpload && !bAllTextureFinish)
		{
			return;
		}

		GLTFMaterialGPUData uploadData{ };

		uploadData.alphaMode                = (uint)reference->alphaMode;
		uploadData.alphaCutOff              = reference->alphaCoutoff;
		uploadData.bTwoSided                = reference->bDoubleSided;
		uploadData.baseColorId              = baseColorTexture->getSRV(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D);
		uploadData.baseColorFactor          = reference->baseColorFactor;
		uploadData.emissiveTexture          = emissiveTexture->getSRV(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D);
		uploadData.emissiveFactor           = reference->emissiveFactor;
		uploadData.metallicFactor           = reference->metallicFactor;
		uploadData.roughnessFactor          = reference->roughnessFactor;
		uploadData.metallicRoughnessTexture = metallicRoughnessTexture->getSRV(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D);
		uploadData.normalTexture            = normalTexture->getSRV(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D);
		uploadData.normalFactorScale        = reference->normalTextureScale;
		uploadData.bExistOcclusion          = reference->bExistOcclusion;
		uploadData.occlusionTextureStrength = reference->occlusionTextureStrength;
		uploadData.baseColorSampler         = reference->baseColorTexture.sampler.getSampler();
		uploadData.normalSampler            = reference->normalTexture.sampler.getSampler();
		uploadData.metallicRoughnessSampler = reference->metallicRoughnessTexture.sampler.getSampler();
		uploadData.emissiveSampler          = reference->emissiveTexture.sampler.getSampler();




		std::array<math::uvec4, GLTFMaterialProxy::kGPUSceneDataFloat4Count> uploadDatas{};
		memcpy(uploadDatas.data(), &uploadData, sizeof(uploadData));

		Application::get().getGPUScene().getGLTFMaterialPool().updateId(m_gpuSceneGLTFMaterialAssetId, uploadDatas);
		if (bAllTextureFinish && uploadData.baseColorId == getContext().getBuiltinTextures().white->getSRV(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D))
		{
			LOG_TRACE("Texture {} used fallback.", reference->baseColorTexture.image.getName().u8());
		}


		enqueueGPUSceneUpdate();
	}

	void GLTFMaterialProxy::freeGPUScene()
	{
		if (m_gpuSceneGLTFMaterialAssetId != -1)
		{
			uint32 id = Application::get().getGPUScene().getGLTFMaterialPool().free(m_proxyId);
			check(id == m_gpuSceneGLTFMaterialAssetId);

			m_gpuSceneGLTFMaterialAssetId = -1;
		}
	}

	uint32 GLTFSampler::getSampler() const
	{
		SamplerCreateInfo ci = buildBasicSamplerCreateInfo();

		switch (minFilter)
		{
		case EMinMagFilter::NEAREST:
		case EMinMagFilter::NEAREST_MIPMAP_NEAREST:
		case EMinMagFilter::NEAREST_MIPMAP_LINEAR:
			ci.minFilter = VK_FILTER_NEAREST;
			break;
		case EMinMagFilter::LINEAR:
		case EMinMagFilter::LINEAR_MIPMAP_NEAREST:
		case EMinMagFilter::LINEAR_MIPMAP_LINEAR:
			ci.minFilter = VK_FILTER_LINEAR;
		default:
			checkEntry();
			break;
		}

		switch (minFilter)
		{
		case EMinMagFilter::NEAREST:
		case EMinMagFilter::NEAREST_MIPMAP_NEAREST:
		case EMinMagFilter::LINEAR_MIPMAP_NEAREST:
			ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			break;
		case EMinMagFilter::LINEAR:
		case EMinMagFilter::NEAREST_MIPMAP_LINEAR:
		case EMinMagFilter::LINEAR_MIPMAP_LINEAR:
			ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		default:
			checkEntry();
			break;
		}

		switch (magFilter)
		{
		case EMinMagFilter::NEAREST:
			ci.magFilter = VK_FILTER_NEAREST;
			break;
		case EMinMagFilter::LINEAR:
			ci.magFilter = VK_FILTER_LINEAR;
			break;
		default:
			checkEntry();
			break;
		}

		switch (wrapS)
		{
		case EWrap::CLAMP_TO_EDGE:
			ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		case EWrap::MIRRORED_REPEAT:
			ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			break;
		case EWrap::REPEAT:
			ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;
		default:
			checkEntry();
			break;
		}

		switch (wrapT)
		{
		case EWrap::CLAMP_TO_EDGE:
			ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		case EWrap::MIRRORED_REPEAT:
			ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			break;
		case EWrap::REPEAT:
			ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;
		default:
			checkEntry();
			break;
		}

		return getContext().getSamplerManager().getSampler(ci).index.get();
	}
}
