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
			explicit ComponentBuffer(
				const std::string& name, 
				VkBufferUsageFlags flags, 
				VmaAllocationCreateFlags vmaFlags, 
				size_t stripe,
				size_t num);

			~ComponentBuffer();

			graphics::GPUBufferRef buffer = nullptr;
			graphics::BindlessIndex bindless;

			VkDeviceSize stripe = ~0U;
			uint32 elementNum   = ~0U;
		};

		std::unique_ptr<ComponentBuffer> indices = nullptr;
		std::unique_ptr<ComponentBuffer> positions = nullptr;
		std::unique_ptr<ComponentBuffer> normals = nullptr;
		std::unique_ptr<ComponentBuffer> uv0s = nullptr;
		std::unique_ptr<ComponentBuffer> tangents = nullptr;

		// Optional
		std::unique_ptr<ComponentBuffer> colors = nullptr;
		std::unique_ptr<ComponentBuffer> uv1s = nullptr;
		std::unique_ptr<ComponentBuffer> smoothNormals = nullptr;

		explicit GPUGLTFPrimitiveAsset(const std::string& name);

		// Current gpu buffer sizes.
		size_t getSize() const;

	private:		
		std::string m_name;

	};
	using GPUGLTFPrimitiveAssetWeak = std::weak_ptr<GPUGLTFPrimitiveAsset>;
	using GPUGLTFPrimitiveAssetRef = std::shared_ptr<GPUGLTFPrimitiveAsset>;
}