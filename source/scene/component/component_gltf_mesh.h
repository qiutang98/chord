#pragma once

#include <scene/component.h>
#include <asset/gltf/asset_gltf.h>
#include <shader/base.h>

namespace chord
{
	class GLTFMeshComponent : public Component
	{
		REGISTER_BODY_DECLARE(Component);
		using Super = Component;

	public:
		static const UIComponentDrawDetails kComponentUIDrawDetails;

		GLTFMeshComponent() = default;
		GLTFMeshComponent(SceneNodeRef sceneNode) : Component(sceneNode) { }

		virtual void onPerViewPerframeCollect(PerframeCollected& collector, const ICamera* camera) const override;
		virtual void postLoad() override;

		virtual void tick(const ApplicationTickData& tickData) override;

		virtual ~GLTFMeshComponent() = default;

		bool setGLTFMesh(const AssetSaveInfo& asset, int32 meshId);
		void setGLTFMaterial(const std::vector<AssetSaveInfo>& assets);
		auto getMaterialProxy(uint32 id) const { return m_gltfMaterialAssetsProxy.at(id); }

	private:
		static UIComponentDrawDetails createComponentUIDrawDetails();

		void reloadMesh();
		void reloadMaterials();

		void buildCacheMeshDrawCommand();
		

	private:
		// Using GLTF asset, host by component.
		GLTFAssetRef m_gltfAsset = nullptr; 

		// Using GLTF primtive GPU Asset.
		GPUGLTFPrimitiveAssetRef m_gltfGPU = nullptr;

		// Proxy of material.
		std::vector<std::shared_ptr<GLTFMaterialProxy>> m_gltfMaterialAssetsProxy;

		struct CacheMeshDrawCommand
		{
			uint32 gltfLod0MeshletCount = 0;
			uint32 gltfMeshletGroupCount = 0;
			std::vector<GPUObjectGLTFPrimitive> gltfPrimitives;
		};
		bool m_cachedDrawCommandNeedLoading = false;
		mutable CacheMeshDrawCommand m_cacheMeshDrawCommand;

	private:
		// Using gltf mesh id.
		int32 m_gltfMeshId = -1;

		// Using GLTF asset info.
		AssetSaveInfo m_gltfAssetInfo;

		// Using GLTF material asset info.
		std::vector<AssetSaveInfo> m_gltfMaterialAssetInfos;
	};
}