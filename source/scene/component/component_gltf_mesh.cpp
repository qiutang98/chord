#include <scene/component/component_gltf_mesh.h>
#include <ui/ui_helper.h>
#include <scene/scene_node.h>

namespace chord
{
	const UIComponentDrawDetails GLTFMeshComponent::kComponentUIDrawDetails = GLTFMeshComponent::createComponentUIDrawDetails();

	UIComponentDrawDetails GLTFMeshComponent::createComponentUIDrawDetails()
	{
		UIComponentDrawDetails result{};

		result.name = "GLTF Mesh";
		result.bOptionalCreated = true;
		result.decoratedName = ICON_FA_BOX + std::string("  GLTF Mesh");
		result.factory = []() { return std::make_shared<GLTFMeshComponent>(); };

		result.onDrawUI = [](ComponentRef component) -> bool
		{
			auto meshComponent = std::static_pointer_cast<GLTFMeshComponent>(component);
			bool bChanged = false;
			
			{
				ui::inspectAssetSaveInfo(meshComponent->m_gltfAssetInfo);
				if (!meshComponent->m_gltfAssetInfo.empty())
				{
					ImGui::TextDisabled("Choose mesh id: #%d.", meshComponent->m_gltfMeshId);
					for (int32 i = 0; i < meshComponent->m_gltfMaterialAssetInfos.size(); i++)
					{
						ui::inspectAssetSaveInfo(meshComponent->m_gltfMaterialAssetInfos[i]);
					}
				}
			}

			return bChanged;
		};

		return result;
	}

	void GLTFMeshComponent::reloadMesh()
	{
		m_cachedDrawCommandNeedLoading = true;
		m_gltfAsset = Application::get().getAssetManager().getOrLoadAsset<GLTFAsset>(m_gltfAssetInfo.path(), true);
		m_gltfGPU = m_gltfAsset->getGPUPrimitives_AnyThread();
	}

	void GLTFMeshComponent::reloadMaterials()
	{
		m_cachedDrawCommandNeedLoading = true;
		m_gltfMaterialAssetsProxy.resize(m_gltfMaterialAssetInfos.size());
		for (int32 i = 0; i < m_gltfMaterialAssetsProxy.size(); i++)
		{
			m_gltfMaterialAssetsProxy[i] = tryLoadGLTFMaterialAsset(m_gltfMaterialAssetInfos[i])->getProxy();
		}
	}

	void GLTFMeshComponent::buildCacheMeshDrawCommand()
	{
		const auto& meshes = m_gltfAsset->getMeshes().at(m_gltfMeshId);

		m_cacheMeshDrawCommand.gltfLod0MeshletCount = 0;
		m_cacheMeshDrawCommand.gltfMeshletGroupCount = 0;

		m_cacheMeshDrawCommand.gltfPrimitives.clear();
		m_cacheMeshDrawCommand.gltfPrimitives.reserve(meshes.primitives.size());

		GPUObjectGLTFPrimitive templatePrimitive{ };

		uint lod0MeshletCount = 0;
		uint meshletGroupCount = 0;
		for (uint32 primitiveId = 0; primitiveId < meshes.primitives.size(); primitiveId++)
		{
			auto materialProxy = getMaterialProxy(primitiveId);

			templatePrimitive.GLTFPrimitiveDetail = m_gltfGPU->getGPUScenePrimitiveDetailId(m_gltfMeshId, primitiveId);
			templatePrimitive.GLTFMaterialData = materialProxy->getGPUSceneId();

			lod0MeshletCount += meshes.primitives[primitiveId].lod0meshletCount;
			meshletGroupCount += meshes.primitives[primitiveId].meshletGroupCount;

			m_cacheMeshDrawCommand.gltfPrimitives.push_back(templatePrimitive);
		}

		m_cacheMeshDrawCommand.gltfLod0MeshletCount = lod0MeshletCount;
		m_cacheMeshDrawCommand.gltfMeshletGroupCount = meshletGroupCount;
	}

	void GLTFMeshComponent::onPerViewPerframeCollect(
		PerframeCollected& collector, 
		const ICamera* camera) const
	{
		Super::onPerViewPerframeCollect(collector, camera);

		const uint32 basePrimitiveObjectId = collector.gltfPrimitives.size();
		const auto& basicData = getNode()->getActiveViewGPUObjectBasicData();
		if (!m_cacheMeshDrawCommand.gltfPrimitives.empty())
		{
			for (auto& prim : m_cacheMeshDrawCommand.gltfPrimitives)
			{
				prim.basicData = basicData;
			}

			collector.gltfPrimitives.insert(collector.gltfPrimitives.end(), m_cacheMeshDrawCommand.gltfPrimitives.begin(), m_cacheMeshDrawCommand.gltfPrimitives.end());
			collector.gltfLod0MeshletCount.fetch_add(m_cacheMeshDrawCommand.gltfLod0MeshletCount);
			collector.gltfMeshletGroupCount.fetch_add(m_cacheMeshDrawCommand.gltfMeshletGroupCount);
		}

		if (m_gltfGPU->isBLASInit())
		{
			math::mat4 temp = math::transpose(basicData.localToTranslatedWorld);
			const auto& cacheBLASInstanceCollector = m_gltfGPU->getBLASInstanceCollector(m_gltfMeshId);

			collector.asInstances.asInstances.insert(collector.asInstances.asInstances.end(), cacheBLASInstanceCollector.asInstances.begin(), cacheBLASInstanceCollector.asInstances.end());
			for (uint32 primitiveId = 0; primitiveId < cacheBLASInstanceCollector.asInstances.size(); primitiveId++)
			{
				auto& instance = collector.asInstances.asInstances[primitiveId + basePrimitiveObjectId];
				instance.instanceCustomIndex = basePrimitiveObjectId + primitiveId;
				memcpy(&instance.transform, &temp, sizeof(VkTransformMatrixKHR));
			}
		}
	}

	bool GLTFMeshComponent::setGLTFMesh(const AssetSaveInfo& asset, int32 meshId)
	{
		if (asset != m_gltfAssetInfo || meshId != m_gltfMeshId)
		{
			m_gltfMeshId = meshId;

			if (asset != m_gltfAssetInfo)
			{
				m_gltfAssetInfo = asset;
				reloadMesh();
			}

			markDirty();
			return true;
		}
		return false;
	}

	void GLTFMeshComponent::postLoad()
	{
		if (!m_gltfAssetInfo.empty())
		{
			reloadMesh();
			reloadMaterials();
		}
	}

	void GLTFMeshComponent::tick(const ApplicationTickData& tickData)
	{
		Super::tick(tickData);

		if (m_gltfGPU && m_gltfAsset && m_gltfMeshId >= 0 && m_gltfGPU->isReady())
		{
			if (m_cachedDrawCommandNeedLoading)
			{
				buildCacheMeshDrawCommand();
				m_cachedDrawCommandNeedLoading = false;
			}
		}
	}

	void GLTFMeshComponent::setGLTFMaterial(const std::vector<AssetSaveInfo>& assets)
	{
		bool bChange = false;
		if (m_gltfMaterialAssetInfos.size() == assets.size())
		{
			for (size_t i = 0; i < assets.size(); i++)
			{
				if (assets[i] != m_gltfMaterialAssetInfos[i])
				{
					bChange = true;
					break;
				}
			}
		}
		else
		{
			bChange = true;
		}

		if (bChange)
		{
			m_gltfMaterialAssetInfos = assets;
			reloadMaterials();
			markDirty();
		}
	}
}