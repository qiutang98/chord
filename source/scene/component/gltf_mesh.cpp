#include <scene/component/gltf_mesh.h>
#include <ui/ui_helper.h>
#include <scene/scene_node.h>
#include <scene/component/gltf_material.h>

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
					ImGui::SameLine(); ImGui::TextDisabled("Choose mesh id: #%d.", meshComponent->m_gltfMeshId);
				}
			}

			return bChanged;
		};

		return result;
	}

	void GLTFMeshComponent::reloadResource()
	{
		m_gltfAsset = Application::get().getAssetManager().getOrLoadAsset<GLTFAsset>(m_gltfAssetInfo.path(), true);
		m_gltfGPU = m_gltfAsset->getGPUPrimitives();
	}

	void GLTFMeshComponent::onPerViewPerframeCollect(PerframeCollected& collector, const PerframeCameraView& cameraView, const ICamera* camera) const
	{
		Super::onPerViewPerframeCollect(collector, cameraView, camera);
		auto materialComp = getNode()->getComponent<GLTFMaterialComponent>();

		if (m_gltfGPU == nullptr || m_gltfAsset == nullptr || m_gltfMeshId < 0)
		{
			return;
		}

		if (!m_gltfGPU->isReady())
		{
			return;
		}

		GPUObjectGLTFPrimitive templatePrimitive { };
		templatePrimitive.basicData = getNode()->getObjectBasicData(cameraView, camera);

		const auto& meshes = m_gltfAsset->getMeshes().at(m_gltfMeshId);

		uint meshletCount = 0;
		for (uint32 primitiveId = 0; primitiveId < meshes.primitives.size(); primitiveId++)
		{
			auto materialProxy = materialComp->getProxy(primitiveId);

			templatePrimitive.GLTFPrimitiveDetail = m_gltfGPU->getGPUScenePrimitiveDetailId(m_gltfMeshId, primitiveId);
			templatePrimitive.GLTFMaterialData = materialProxy->getGPUSceneId();

			meshletCount += meshes.primitives[primitiveId].lod0MeshletCount;
			collector.gltfPrimitives.push_back(templatePrimitive);
		}

		collector.gltfLod0MeshletCount.fetch_add(meshletCount);
	}

	bool GLTFMeshComponent::setGLTFMesh(const AssetSaveInfo& asset, int32 meshId)
	{
		if (asset != m_gltfAssetInfo || meshId != m_gltfMeshId)
		{
			m_gltfMeshId = meshId;

			if (asset != m_gltfAssetInfo)
			{
				m_gltfAssetInfo = asset;
				reloadResource();
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
			reloadResource();
		}
	}
}