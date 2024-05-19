#pragma once
#include <scene/component.h>

#include <asset/gltf/gltf.h>

namespace chord
{
	class GLTFMeshComponent : public Component
	{
		REGISTER_BODY_DECLARE(Component);

	public:
		static const UIComponentDrawDetails kComponentUIDrawDetails;

		GLTFMeshComponent() = default;
		GLTFMeshComponent(SceneNodeRef sceneNode) : Component(sceneNode) { }

		virtual ~GLTFMeshComponent() = default;

		bool setGLTFMesh(const AssetSaveInfo& asset, int32 meshId);

	private:
		static UIComponentDrawDetails createComponentUIDrawDetails();

	private:
		GLTFAssetRef m_gltfAsset; // Using gltf asset, host by component.

	private:
		// Using gltf mesh id.
		int32 m_gltfMeshId;

		// Using GLTF asset info.
		AssetSaveInfo m_gltfAssetInfo;
	};
}