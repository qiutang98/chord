#pragma once

#include <asset/asset.h>
#include <asset/asset_common.h>
#include <shader/gltf.h>

namespace chord
{
	struct GLTFAssetImportConfig : public IAssetImportConfig
	{
		bool bGenerateSmoothNormal = false;
		bool bFuse = false;
		float fuseRelativeDistance = 1e-5f;

		float meshletConeWeight = 0.7f;
	};
	using GLTFAssetImportConfigRef = std::shared_ptr<GLTFAssetImportConfig>;
	class GLTFAsset;

	struct ComponentBuffer
	{
		explicit ComponentBuffer(
			const std::string& name,
			VkBufferUsageFlags flags,
			VmaAllocationCreateFlags vmaFlags,
			uint32 stripe,
			uint32 num);

		~ComponentBuffer();

		graphics::GPUBufferRef buffer = nullptr;
		graphics::BindlessIndex bindless;

		VkDeviceSize stripe = ~0U;
		uint32 elementNum = ~0U;
	};

	// Mesh primitive asset upload to gpu. Remap to GLTFPrimitiveDatasBuffer.
	// GPU proxy of gltf primitive.
	class GPUGLTFPrimitiveAsset : public graphics::IUploadAsset
	{
	public:
		constexpr static uint32 kGPUSceneDataFloat4Count = CHORD_DIVIDE_AND_ROUND_UP(sizeof(GLTFPrimitiveDatasBuffer), sizeof(float) * 4);
		constexpr static uint32 kGPUSceneDetailFloat4Count = CHORD_DIVIDE_AND_ROUND_UP(sizeof(GLTFPrimitiveBuffer), sizeof(float) * 4);

		uint64 GPUSceneHash() const
		{
			return hash();
		}

		std::unique_ptr<ComponentBuffer> positions = nullptr;
		std::unique_ptr<ComponentBuffer> normals = nullptr;
		std::unique_ptr<ComponentBuffer> uv0s = nullptr;
		std::unique_ptr<ComponentBuffer> tangents = nullptr;
		std::unique_ptr<ComponentBuffer> meshlet = nullptr;
		std::unique_ptr<ComponentBuffer> meshletData = nullptr;

		// Optional
		std::unique_ptr<ComponentBuffer> colors = nullptr;
		std::unique_ptr<ComponentBuffer> uv1s = nullptr;
		std::unique_ptr<ComponentBuffer> smoothNormals = nullptr;

		explicit GPUGLTFPrimitiveAsset(const std::string& name, std::shared_ptr<GLTFAsset> asset);
		virtual ~GPUGLTFPrimitiveAsset();

		// Current gpu buffer sizes.
		uint32 getSize() const;

		// Push data in GPU scene.
		void updateGPUScene();
		void freeGPUScene();

		uint32 getGPUSceneId() const;
		uint32 getGPUScenePrimitiveDetailId(uint32 meshId, uint32 primitiveId) const;

	private:		
		uint32 m_gpuSceneGLTFPrimitiveAssetId = -1;
		std::vector<std::vector<uint32>> m_gpuSceneGLTFPrimitiveDetailAssetId = {};
		std::weak_ptr<GLTFAsset> m_gltfAssetWeak = {};
		std::string m_name;
	};
	using GPUGLTFPrimitiveAssetWeak = std::weak_ptr<GPUGLTFPrimitiveAsset>;
	using GPUGLTFPrimitiveAssetRef = std::shared_ptr<GPUGLTFPrimitiveAsset>;
}