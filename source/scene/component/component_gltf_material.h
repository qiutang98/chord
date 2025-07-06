#pragma once

#include <scene/component.h>
#include <asset/gltf/asset_gltf.h>
#include <asset/texture/asset_texture.h>

namespace chord
{


	class GLTFMaterialComponent : public Component
	{
		REGISTER_BODY_DECLARE(Component);
		using Super = Component;

	public:
		static const UIComponentDrawDetails kComponentUIDrawDetails;

		GLTFMaterialComponent() = default;
		GLTFMaterialComponent(SceneNodeRef sceneNode) : Component(sceneNode) { }

		virtual ~GLTFMaterialComponent() = default;

		bool setGLTFMaterial(const std::vector<AssetSaveInfo>& assets);
		virtual void postLoad() override;

		std::shared_ptr<GLTFMaterialProxy> getProxy(uint32 id) const
		{
			return m_gltfMaterialAssetsProxy.at(id);
		}

	private:
		void reloadResource();
		static UIComponentDrawDetails createComponentUIDrawDetails();

		// Proxy of material.
		std::vector<std::shared_ptr<GLTFMaterialProxy>> m_gltfMaterialAssetsProxy;

	private:
		// Using GLTF material asset info.
		std::vector<AssetSaveInfo> m_gltfAssetInfos;
	};
}