#include <asset/gltf/gltf.h>
#include <asset/gltf/gltf_helper.h>
#include <asset/serialize.h>
#include <renderer/gpu_scene.h>
#include <shader/base.h>

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

					newGPUPrimitives->meshlet = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_meshlet"),
						bufferFlagBasic,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.meshlets[0]),
						(uint32)gltfBin.primitiveData.meshlets.size());
					copyBuffer(*newGPUPrimitives->meshlet, gltfBin.primitiveData.meshlets.data());

					newGPUPrimitives->meshletData = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_meshletData"),
						bufferFlagBasic,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.meshletDatas[0]),
						(uint32)gltfBin.primitiveData.meshletDatas.size());
					copyBuffer(*newGPUPrimitives->meshletData, gltfBin.primitiveData.meshletDatas.data());

					check(!gltfBin.primitiveData.bvhNodes.empty());
					{
						newGPUPrimitives->bvhNodeData = std::make_unique<ComponentBuffer>(
							getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_bvhNodes"),
							bufferFlagBasic,
							bufferFlagVMA,
							(uint32)sizeof(gltfBin.primitiveData.bvhNodes[0]),
							(uint32)gltfBin.primitiveData.bvhNodes.size());
						copyBuffer(*newGPUPrimitives->bvhNodeData, gltfBin.primitiveData.bvhNodes.data());
					}

					newGPUPrimitives->meshletGroup = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_meshletGroup"),
						bufferFlagBasic,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.meshletGroups[0]),
						(uint32)gltfBin.primitiveData.meshletGroups.size());
					copyBuffer(*newGPUPrimitives->meshletGroup, gltfBin.primitiveData.meshletGroups.data());

					newGPUPrimitives->meshletGroupIndices = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_meshletGroupIndices"),
						bufferFlagBasic,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.meshletGroupIndices[0]),
						(uint32)gltfBin.primitiveData.meshletGroupIndices.size());
					copyBuffer(*newGPUPrimitives->meshletGroupIndices, gltfBin.primitiveData.meshletGroupIndices.data());

					newGPUPrimitives->lod0Indices = std::make_unique<ComponentBuffer>(
						getRuntimeUniqueGPUAssetName(assetPtr->getName().u8() + "_lod0Indices"),
						bufferFlagBasic,
						bufferFlagVMA,
						(uint32)sizeof(gltfBin.primitiveData.lod0Indices[0]),
						(uint32)gltfBin.primitiveData.lod0Indices.size());
					copyBuffer(*newGPUPrimitives->lod0Indices, gltfBin.primitiveData.lod0Indices.data());

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
				newGPUPrimitives->buildBLAS();
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
		auto whiteTex = getContext().getBuiltinResources().white;
		auto transparentTex = getContext().getBuiltinResources().transparent;
		auto normalTex = getContext().getBuiltinResources().normal;
		std::weak_ptr<GLTFMaterialProxy> weakPtr = proxy;

		// Load texture.
		auto proxyLoadTexture = [&](const auto& materialTex, auto& tex, GPUTextureAssetRef fallback)
		{
			tex.bExist = materialTex.isValid();
			if (tex.bExist)
			{
				auto asset = tryLoadTextureAsset(materialTex.image);
				tex.texture = asset->getGPUTexture([weakPtr](GPUTextureAssetRef texture)
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
				tex.texture = fallback;
			}
		};

		// Load all needed texture.
		proxyLoadTexture(proxy->reference->baseColorTexture, proxy->baseColorTexture, whiteTex);
		proxyLoadTexture(proxy->reference->metallicRoughnessTexture, proxy->metallicRoughnessTexture, whiteTex);
		proxyLoadTexture(proxy->reference->emissiveTexture, proxy->emissiveTexture, transparentTex);
		proxyLoadTexture(proxy->reference->normalTexture, proxy->normalTexture, normalTex);

		// Require gpu scene space.
		proxy->m_gpuSceneGLTFMaterialAssetId = Application::get().getGPUScene().getGLTFMaterialPool().requireId(proxy->m_proxyId);
		proxy->updateGPUScene(true);
	}

	void GLTFMaterialProxy::updateGPUScene(bool bForceUpload)
	{
		const bool bAllTextureFinish = 
			baseColorTexture.isLoadingReady() &&
			emissiveTexture.isLoadingReady()  &&
			normalTexture.isLoadingReady()    &&
			metallicRoughnessTexture.isLoadingReady();

		if (!bForceUpload && !bAllTextureFinish)
		{
			return;
		}

		GLTFMaterialGPUData uploadData{ };

		uploadData.alphaMode                = (uint)reference->alphaMode;
		uploadData.alphaCutOff              = reference->alphaCoutoff;
		uploadData.bTwoSided                = reference->bDoubleSided;
		uploadData.baseColorId              = baseColorTexture.requireSRV();
		uploadData.baseColorFactor          = reference->baseColorFactor;
		uploadData.emissiveTexture          = emissiveTexture.requireSRV();
		uploadData.emissiveFactor           = reference->emissiveFactor;
		uploadData.metallicFactor           = reference->metallicFactor;
		uploadData.roughnessFactor          = reference->roughnessFactor;
		uploadData.metallicRoughnessTexture = metallicRoughnessTexture.requireSRV(true);
		uploadData.normalTexture            = normalTexture.requireSRV(true);
		uploadData.normalFactorScale        = reference->normalTextureScale;
		uploadData.bExistOcclusion          = reference->bExistOcclusion;
		uploadData.occlusionTextureStrength = reference->occlusionTextureStrength;
		uploadData.baseColorSampler         = reference->baseColorTexture.sampler.getSampler();
		uploadData.normalSampler            = reference->normalTexture.sampler.getSampler();
		uploadData.metallicRoughnessSampler = reference->metallicRoughnessTexture.sampler.getSampler();
		uploadData.emissiveSampler          = reference->emissiveTexture.sampler.getSampler();
		uploadData.materialType             = (uint)reference->shadingType;



		std::array<math::uvec4, GLTFMaterialProxy::kGPUSceneDataFloat4Count> uploadDatas{};
		memcpy(uploadDatas.data(), &uploadData, sizeof(uploadData));

		Application::get().getGPUScene().getGLTFMaterialPool().updateId(m_gpuSceneGLTFMaterialAssetId, uploadDatas);
		if (bAllTextureFinish && uploadData.baseColorId == getContext().getBuiltinResources().white->getSRV(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D))
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
		ci.anisotropyEnable = VK_TRUE;
		ci.maxAnisotropy    = 8.0f;

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
			break;
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
			break;
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

	uint32 GPUGLTFPrimitiveAsset::getGPUSceneId() const
	{
		return m_gpuSceneGLTFPrimitiveAssetId;
	}

	uint32 GPUGLTFPrimitiveAsset::getGPUScenePrimitiveDetailId(uint32 meshId, uint32 primitiveId) const
	{
		return m_gpuSceneGLTFPrimitiveDetailAssetId.at(meshId).at(primitiveId);
	}

	static inline uint32 getGLTFPrimitiveBLASIndex(uint meshIndex, uint primitiveId, uint meshCount)
	{

	}

	// Current BVH from LOD0, maybe we can reduce lod level.
	void GPUGLTFPrimitiveAsset::buildBLAS()
	{
		if (!getContext().isRaytraceSupport())
		{
			return;
		}

		auto assetPtr = m_gltfAssetWeak.lock();
		if(assetPtr == nullptr) 
		{
			return;
		}

		if (m_blasBuilder.isInit())
		{
			return;
		}

		using namespace graphics;
		using namespace graphics::helper;

		// Clear mesh primitive id.
		m_meshPrimIdMap = {};
		const auto& meshes = assetPtr->getMeshes();

		std::vector<BLASBuilder::BlasInput> allBlas{ };

		for (uint32 meshIndex = 0; meshIndex < meshes.size(); meshIndex++)
		{
			const auto& primitives = meshes[meshIndex].primitives;
			for (uint32 primitiveId = 0; primitiveId < primitives.size(); primitiveId++)
			{
				const auto& gltfPrimtive = primitives[primitiveId];

				GLTFPrimitiveIndexing indexing{ .meshId = meshIndex, .primitiveId = primitiveId };
				m_meshPrimIdMap[indexing.getHash()] = allBlas.size();

				BLASBuilder::BlasInput subMesh{ };

				{
					// Describe buffer as array of VertexObj.
					VkAccelerationStructureGeometryTrianglesDataKHR triangles{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
					triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;  // float3 vertex position data.
					triangles.vertexData.deviceAddress = positions->buffer->getDeviceAddress();
					triangles.vertexStride = positions->stripe;
					triangles.indexType = VK_INDEX_TYPE_UINT32;
					triangles.indexData.deviceAddress = lod0Indices->buffer->getDeviceAddress();
					triangles.maxVertex = gltfPrimtive.vertexCount;

					// Identify the above data as containing opaque triangles.
					VkAccelerationStructureGeometryKHR asGeom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
					asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
					asGeom.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
					asGeom.geometry.triangles = triangles;

					VkAccelerationStructureBuildRangeInfoKHR offset{ };
					offset.firstVertex = gltfPrimtive.vertexOffset;
					offset.primitiveCount = gltfPrimtive.lod0IndicesCount / 3;
					offset.primitiveOffset = gltfPrimtive.lod0IndicesOffset * sizeof(uint32);
					offset.transformOffset = 0;

					subMesh.asGeometry.emplace_back(asGeom);
					subMesh.asBuildOffsetInfo.emplace_back(offset);
				}

				allBlas.push_back(std::move(subMesh));
			}
		}

		// 
		m_blasBuilder.build(allBlas, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR);
	}

	const VkDeviceAddress GPUGLTFPrimitiveAsset::getBLASDeviceAddress(uint32 meshId, uint32 primitiveId) const
	{
		check(getContext().isRaytraceSupport());

		GLTFPrimitiveIndexing indexing{ .meshId = meshId, .primitiveId = primitiveId };

		uint objectId = m_meshPrimIdMap.at(indexing.getHash());
		return m_blasBuilder.getBlasDeviceAddress(objectId);
	}

	static inline uint64 getPrimitiveDetailHash(uint64 GPUSceneHash, uint32 meshId, uint32 primitiveId)
	{
		uint64 pack = (uint64(meshId) << 32) | primitiveId;
		return hashCombine(pack, GPUSceneHash);
	}

	void GPUGLTFPrimitiveAsset::freeGPUScene()
	{
		check(isInMainThread());

		if (m_gpuSceneGLTFPrimitiveAssetId != -1)
		{
			auto& gltfPrimitiveAssetPool = Application::get().getGPUScene().getGLTFPrimitiveDataPool();

			uint32 id = gltfPrimitiveAssetPool.free(GPUSceneHash());
			check(id == m_gpuSceneGLTFPrimitiveAssetId);

			m_gpuSceneGLTFPrimitiveAssetId = -1;
		}

		auto& gltfPrimitiveDetailPool = Application::get().getGPUScene().getGLTFPrimitiveDetailPool();
		for (uint32 meshId = 0; meshId < m_gpuSceneGLTFPrimitiveDetailAssetId.size(); meshId++)
		{
			const auto& primitiveInfos = m_gpuSceneGLTFPrimitiveDetailAssetId[meshId];
			for (uint32 primitiveId = 0; primitiveId < primitiveInfos.size(); primitiveId++)
			{
				const auto hash = getPrimitiveDetailHash(GPUSceneHash(), meshId, primitiveId);

				uint32 freeId = gltfPrimitiveDetailPool.free(hash);
				check(freeId == primitiveInfos[primitiveId]);
			}
		}
		m_gpuSceneGLTFPrimitiveDetailAssetId.clear();
	}

	

	void GPUGLTFPrimitiveAsset::updateGPUScene()
	{
		check(isInMainThread());
		freeGPUScene();

		auto& gltfPrimitiveAssetPool = Application::get().getGPUScene().getGLTFPrimitiveDataPool();
		m_gpuSceneGLTFPrimitiveAssetId = gltfPrimitiveAssetPool.requireId(GPUSceneHash());

		GLTFPrimitiveDatasBuffer uploadData { };

		#define ASSIGN_DATA(A, B) if(A != nullptr) { uploadData.B = A->bindless.get(); } else { uploadData.B = ~0; }
		{
			ASSIGN_DATA(positions, positionBuffer);
			ASSIGN_DATA(normals, normalBuffer);
			ASSIGN_DATA(uv0s, textureCoord0Buffer);
			ASSIGN_DATA(tangents, tangentBuffer);
			ASSIGN_DATA(colors, color0Buffer);
			ASSIGN_DATA(uv1s, textureCoord1Buffer);
			ASSIGN_DATA(smoothNormals, smoothNormalsBuffer);
			ASSIGN_DATA(meshlet, meshletBuffer);
			ASSIGN_DATA(meshletData, meshletDataBuffer);
			ASSIGN_DATA(bvhNodeData, bvhNodeBuffer);
			ASSIGN_DATA(meshletGroup, meshletGroupBuffer);
			ASSIGN_DATA(meshletGroupIndices, meshletGroupIndicesBuffer);
			ASSIGN_DATA(lod0Indices, lod0IndicesBuffer);
		}
		#undef ASSIGN_DATA

		{
			std::array<math::uvec4, GPUGLTFPrimitiveAsset::kGPUSceneDataFloat4Count> uploadDatas{};
			memcpy(uploadDatas.data(), &uploadData, sizeof(uploadData));

			gltfPrimitiveAssetPool.updateId(m_gpuSceneGLTFPrimitiveAssetId, uploadDatas);
		}

		//
		auto assetRef = m_gltfAssetWeak.lock();
		m_gpuSceneGLTFPrimitiveDetailAssetId.resize(assetRef->getMeshes().size());

		auto& gltfPrimitiveDetailPool = Application::get().getGPUScene().getGLTFPrimitiveDetailPool();
		for (uint32 meshId = 0; meshId < assetRef->getMeshes().size(); meshId++)
		{
			auto& meshPrimitiveIds = m_gpuSceneGLTFPrimitiveDetailAssetId[meshId];
			const auto& meshInfo = assetRef->getMeshes().at(meshId);
			meshPrimitiveIds.resize(meshInfo.primitives.size());
			for (uint32 primitiveId = 0; primitiveId < meshInfo.primitives.size(); primitiveId++)
			{
				// Require GPU scene id.
				const auto primitiveHash = getPrimitiveDetailHash(GPUSceneHash(), meshId, primitiveId);
				meshPrimitiveIds[primitiveId] = gltfPrimitiveDetailPool.requireId(primitiveHash);

				const auto& primitiveInfo = meshInfo.primitives[primitiveId];

				// Fill upload content.
				GLTFPrimitiveBuffer primitiveBufferData{};
				{
					primitiveBufferData.posMin                    = primitiveInfo.posMin;
					primitiveBufferData.primitiveDatasBufferId    = m_gpuSceneGLTFPrimitiveAssetId;
					primitiveBufferData.posMax                    = primitiveInfo.posMax;
					primitiveBufferData.vertexOffset              = primitiveInfo.vertexOffset;
					primitiveBufferData.posAverage                = primitiveInfo.posAverage;
					primitiveBufferData.vertexCount               = primitiveInfo.vertexCount;
					primitiveBufferData.color0Offset              = primitiveInfo.colors0Offset;
					primitiveBufferData.smoothNormalOffset        = primitiveInfo.smoothNormalOffset;
					primitiveBufferData.textureCoord1Offset       = primitiveInfo.textureCoord1Offset;
					primitiveBufferData.meshletOffset             = primitiveInfo.meshletOffset;
					primitiveBufferData.bvhNodeOffset             = primitiveInfo.bvhNodeOffset;
					primitiveBufferData.meshletGroupOffset        = primitiveInfo.meshletGroupOffset;
					primitiveBufferData.meshletGroupIndicesOffset = primitiveInfo.meshletGroupIndicesOffset;
					primitiveBufferData.meshletGroupCount         = primitiveInfo.meshletGroupCount;
					primitiveBufferData.lod0IndicesCount          = primitiveInfo.lod0IndicesCount;
					primitiveBufferData.lod0IndicesOffset         = primitiveInfo.lod0IndicesOffset;
				}

				std::array<math::uvec4, GPUGLTFPrimitiveAsset::kGPUSceneDetailFloat4Count> uploadDatas{};
				memcpy(uploadDatas.data(), &primitiveBufferData, sizeof(primitiveBufferData));

				// Updata to GPUScene.
				gltfPrimitiveDetailPool.updateId(meshPrimitiveIds[primitiveId], uploadDatas);
			}
		}

		enqueueGPUSceneUpdate();
	}

	uint32 GPUGLTFPrimitiveAsset::getSize() const
	{
		auto getValidSize = [](const std::unique_ptr<ComponentBuffer>& buffer) -> uint32 
		{ 
			if (buffer != nullptr) 
			{ 
				return buffer->buffer->getSize(); 
			} 
			return 0;
		};

		return
			getValidSize(positions)
			+ getValidSize(normals)
			+ getValidSize(uv0s)
			+ getValidSize(tangents)
			+ getValidSize(colors)
			+ getValidSize(uv1s)
			+ getValidSize(smoothNormals)
			+ getValidSize(meshlet)
			+ getValidSize(meshletData)
			+ getValidSize(bvhNodeData)
			+ getValidSize(meshletGroup)
			+ getValidSize(meshletGroupIndices)
			+ getValidSize(lod0Indices);
	}

	uint32 GLTFMaterialProxy::TextureInfo::requireSRV(bool bReturnUnValidIfNoExist) const
	{
		if (!bExist && bReturnUnValidIfNoExist)
		{
			return kUnvalidIdUint32;
		}

		checkMsgf(texture != nullptr, "Texture must create before require srv.");
		return texture->getSRV(graphics::helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D);
	}
}
