#include <scene/component/gltf_material.h>
#include <ui/ui_helper.h>
#include <scene/scene_node.h>

namespace chord
{
	const UIComponentDrawDetails GLTFMaterialComponent::kComponentUIDrawDetails = GLTFMaterialComponent::createComponentUIDrawDetails();
	UIComponentDrawDetails GLTFMaterialComponent::createComponentUIDrawDetails()
	{
		UIComponentDrawDetails result{};

		result.name = "GLTF Material";
		result.bOptionalCreated = true;
		result.decoratedName = ICON_FA_PALETTE + std::string("  GLTF Material");
		result.factory = []() { return std::make_shared<GLTFMaterialComponent>(); };

		result.onDrawUI = [](ComponentRef component) -> bool
		{
			auto materialComponent = std::static_pointer_cast<GLTFMaterialComponent>(component);
			bool bChanged = false;
			
			if (materialComponent->m_gltfAssetInfos.empty())
			{
				ImGui::TextDisabled("No GLTF material assigned yet...");
			}
			else
			{
				for (int32 i = 0; i < materialComponent->m_gltfAssetInfos.size(); i++)
				{
					ui::inspectAssetSaveInfo(materialComponent->m_gltfAssetInfos[i]);
				}
			}

			return bChanged;
		};

		return result;
	}

	void GLTFMaterialComponent::reloadResource()
	{
		m_gltfMaterialAssetsProxy.resize(m_gltfAssetInfos.size());
		for (int32 i = 0; i < m_gltfMaterialAssetsProxy.size(); i++)
		{
			m_gltfMaterialAssetsProxy[i] = tryLoadGLTFMaterialAsset(m_gltfAssetInfos[i])->getProxy();
		}
	}

	bool GLTFMaterialComponent::setGLTFMaterial(const std::vector<AssetSaveInfo>& assets)
	{
		if (assets != m_gltfAssetInfos)
		{
			m_gltfAssetInfos = assets;
			reloadResource();

			markDirty();
			return true;
		}

		return false;
	}

	void GLTFMaterialComponent::postLoad()
	{
		if (!m_gltfAssetInfos.empty())
		{
			reloadResource();
		}
	}
}


