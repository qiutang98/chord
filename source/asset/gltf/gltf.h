#pragma once
#include <asset/asset.h>

namespace chord
{
	struct GLTFTextureInfo
	{
		AssetSaveInfo image;
		int32 textureCoord = 0;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(image, textureCoord);
		}
	};

	enum class EAlphaMode
	{
		Opaque,
		Mask,
		Blend,
	};

	class GLTFMaterialAsset : public IAsset
	{
		REGISTER_BODY_DECLARE(IAsset);
		friend class AssetManager;
	public:
		static const AssetTypeMeta kAssetTypeMeta;

	private:
		static AssetTypeMeta createTypeMeta();


	public:
		GLTFMaterialAsset() = default;
		virtual ~GLTFMaterialAsset() = default;

		// 
		explicit GLTFMaterialAsset(const AssetSaveInfo& saveInfo);

	protected:
		// ~IAsset virtual function.
		// Call back when call AssetManager::createAsset
		virtual void onPostConstruct() override;

		virtual bool onSave() override;
		virtual void onUnload() override;
		// ~IAsset virtual function.

	public:
		math::vec4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
		GLTFTextureInfo baseColorTexture;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		GLTFTextureInfo metallicRoughnessTexture;
		GLTFTextureInfo emissiveTexture;
		math::vec3 emissiveFactor = {0.0f, 0.0f, 0.0f};

		EAlphaMode alphaMode = EAlphaMode::Opaque;

		float alphaCoutoff = 0.5f;
		bool bDoubleSided = false;

		GLTFTextureInfo normalTexture;
		float normalTextureScale = 1.0f;
		GLTFTextureInfo occlusionTexture;
		float occlusionTextureStrength = 1.0f;
	};

	struct GLTFPrimitiveMesh
	{
		std::string name;

		// Current primitive use which material.
		AssetSaveInfo material;

		uint32 firstIndex   = 0;
		uint32 indexCount   = 0;
		uint32 vertexOffset = 0; // used for required attributes.
		uint32 vertexCount  = 0;

		uint32 colors0Offset = 0;
		uint32 textureCoord1Offset = 0;

		// 
		math::vec3 posMin;
		math::vec3 posMax;
		math::vec3 posAverage;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(name, material, firstIndex, indexCount, vertexCount, vertexOffset);
			ar(posMin, posMax, posAverage, colors0Offset, textureCoord1Offset);
		}
	};

	struct GLTFBinary
	{
		std::vector<GLTFPrimitiveMesh> primMeshes;

		struct
		{
			std::vector<uint32> indices;

			// required.
			std::vector<math::vec3> positions;
			std::vector<math::vec3> normals;
			std::vector<math::vec3> smoothNormals;
			std::vector<math::vec2> texcoords0; 
			std::vector<math::vec4> tangents;

			// optional.
			std::vector<math::vec2> texcoords1;    
			std::vector<math::vec4> colors0;      

		} primitiveData;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(primMeshes);

			ar(primitiveData.indices);

			ar(primitiveData.positions);
			ar(primitiveData.normals);
			ar(primitiveData.smoothNormals);
			ar(primitiveData.texcoords0);
			ar(primitiveData.tangents);

			ar(primitiveData.texcoords1);
			ar(primitiveData.colors0);
		}
	};

	class GLTFAsset : public IAsset
	{
		REGISTER_BODY_DECLARE(IAsset);

		friend class AssetManager;
	public:
		static const AssetTypeMeta kAssetTypeMeta;

	private:
		static AssetTypeMeta createTypeMeta();

	public:
		GLTFAsset() = default;
		virtual ~GLTFAsset() = default;

		// 
		explicit GLTFAsset(const AssetSaveInfo& saveInfo);

	protected:
		// ~IAsset virtual function.
		// Call back when call AssetManager::createAsset
		virtual void onPostConstruct() override;

		virtual bool onSave() override;
		virtual void onUnload() override;
		// ~IAsset virtual function.

	private:
		
	};
	using GLTFAssetRef = std::shared_ptr<GLTFAsset>;
}