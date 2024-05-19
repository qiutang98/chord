#pragma once

#include <asset/asset.h>
#include <asset/asset_common.h>

namespace chord
{
	struct GLTFAssetImportConfig : public IAssetImportConfig
	{
		bool bGenerateSmoothNormal = false;
	};
	using GLTFAssetImportConfigRef = std::shared_ptr<GLTFAssetImportConfig>;

	// Mesh primitive asset upload to gpu.
	class GPUGLTFPrimitiveAsset : public graphics::IUploadAsset
	{
	public:

		struct ComponentBuffer
		{
			graphics::GPUBufferRef buffer = nullptr;
			graphics::BindlessIndex bindless;
			VkDeviceSize stripe = ~0U;
			uint32 elementNum   = ~0U;
		};

		ComponentBuffer indices;
		ComponentBuffer positions;
		ComponentBuffer normals;
		ComponentBuffer uv0s;
		ComponentBuffer tangents;

		// Optional
		ComponentBuffer colors;
		ComponentBuffer uv1s;
		ComponentBuffer smoothNormals;

		explicit GPUGLTFPrimitiveAsset(const std::string& name);
		virtual ~GPUGLTFPrimitiveAsset();

		// Current gpu buffer sizes.
		size_t getSize() const;

	private:
		void makeComponent(const std::string& name, ComponentBuffer& comp, VkBufferUsageFlags flags, VmaAllocationCreateFlags vmaFlags, uint32 stripe, uint32 num);
		void freeComponent(ComponentBuffer& comp, graphics::GPUBufferRef fallback);

	private:		
		std::string m_name;

	};
	using GPUGLTFPrimitiveAssetWeak = std::weak_ptr<GPUGLTFPrimitiveAsset>;
	using GPUGLTFPrimitiveAssetRef = std::shared_ptr<GPUGLTFPrimitiveAsset>;
}