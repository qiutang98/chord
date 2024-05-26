#pragma once
#include <asset/asset.h>
#include <asset/gltf/gltf_helper.h>

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

	struct GLTFPrimitive
	{
		std::string name;

		// Current primitive use which material.
		AssetSaveInfo material;

		uint32 firstIndex   = 0;
		uint32 indexCount   = 0;
		uint32 vertexOffset = 0; // used for required attributes.
		uint32 vertexCount  = 0;

		bool bColor0Exist = false;
		bool bSmoothNormalExist = false;
		bool bTextureCoord1Exist = false;

		// Optional attributes.
		uint32 colors0Offset = 0;
		uint32 smoothNormalOffset = 0;
		uint32 textureCoord1Offset = 0;

		// 
		math::vec3 posMin;
		math::vec3 posMax;
		math::vec3 posAverage;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(name, material, firstIndex, indexCount, vertexCount, vertexOffset);
			ar(posMin, posMax, posAverage, colors0Offset, textureCoord1Offset, smoothNormalOffset);
			ar(bColor0Exist, bSmoothNormalExist, bTextureCoord1Exist);
		}
	};

	struct GLTFMesh
	{
		std::string name;
		std::vector<GLTFPrimitive> primitives;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(name, primitives);
		}
	};

	struct GLTFNode
	{
		std::string name;
		std::vector<int32> childrenIds;

		int32 mesh;
		math::dmat4 localMatrix;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(name, childrenIds, localMatrix, mesh);
		}
	};

	struct GLTFScene
	{
		std::string name;

		std::vector<int32> nodes;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(name, nodes);
		}
	};

	struct GLTFBinary
	{
		// Data from primitive accessor.
		struct PrimitiveDatas
		{
			std::vector<uint32> indices;

			// required.
			std::vector<math::vec3> positions;
			std::vector<math::vec3> normals;
			std::vector<math::vec2> texcoords0; 
			std::vector<math::vec4> tangents;

			// optional.
			std::vector<math::vec2> texcoords1;    
			std::vector<math::vec4> colors0;    
			std::vector<math::vec3> smoothNormals;

			size_t size() const
			{
				auto sizeofV = [](const auto& a) { return a.size() * sizeof(a[0]); };
				return
					  sizeofV(indices)
					+ sizeofV(positions)
					+ sizeofV(normals)
					+ sizeofV(texcoords0)
					+ sizeofV(tangents)
					+ sizeofV(texcoords1)
					+ sizeofV(colors0)
					+ sizeofV(smoothNormals);
			}
		} primitiveData;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(primitiveData.indices);
			ar(primitiveData.positions);
			ar(primitiveData.normals);
			ar(primitiveData.texcoords0);
			ar(primitiveData.tangents);
			ar(primitiveData.smoothNormals);
			ar(primitiveData.texcoords1);
			ar(primitiveData.colors0);
		}
	};

	class GLTFAsset : public IAsset
	{
		REGISTER_BODY_DECLARE(IAsset);

		friend class AssetManager;
		friend bool importFromConfig(GLTFAssetImportConfigRef config);
	public:
		static const AssetTypeMeta kAssetTypeMeta;

	private:
		static AssetTypeMeta createTypeMeta();

	public:
		GLTFAsset() = default;
		virtual ~GLTFAsset() = default;

		// 
		explicit GLTFAsset(const AssetSaveInfo& saveInfo);

		const GLTFScene& getScene() const
		{
			return m_scenes[m_defaultScene > -1 ? m_defaultScene : 0];
		}

		const auto& getNodes() const { return m_nodes; }
		const auto& getMeshes() const { return m_meshes; }

		bool isGPUPrimitivesStreamingReady() const;
		GPUGLTFPrimitiveAssetRef getGPUPrimitives();

	protected:
		// ~IAsset virtual function.
		// Call back when call AssetManager::createAsset
		virtual void onPostConstruct() override;

		virtual bool onSave() override;
		virtual void onUnload() override;
		// ~IAsset virtual function.

	private:
		// Reference host by component, asset only host weak pointer.
		GPUGLTFPrimitiveAssetWeak m_gpuPrimitives;


	private:
		int32 m_defaultScene;
		std::vector<GLTFScene> m_scenes;

		std::vector<GLTFMesh> m_meshes;
		std::vector<GLTFNode> m_nodes;

		size_t m_gltfBinSize;
	};
	using GLTFAssetRef = std::shared_ptr<GLTFAsset>;
	using GLTFAssetWeak = std::weak_ptr<GLTFAsset>;
}