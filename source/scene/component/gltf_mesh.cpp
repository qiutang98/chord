#include <scene/component/gltf_mesh.h>
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
					ImGui::SameLine(); ImGui::TextDisabled("Choose mesh id: #%d.", meshComponent->m_gltfMeshId);
				}
			}

			return bChanged;
		};

		return result;
	}

	void GLTFMeshComponent::onPerframeCollect(PerframeCollected& collector) const
	{
		Super::onPerframeCollect(collector);

		if (m_gltfGPU == nullptr || m_gltfAsset == nullptr)
		{
			return;
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

				// Reload asset.
				m_gltfAsset = Application::get().getAssetManager().getOrLoadAsset<GLTFAsset>(asset.path(), true);
				m_gltfGPU = m_gltfAsset->getGPUPrimitives();
			}

			markDirty();

			return true;
		}
		return false;
	}
}