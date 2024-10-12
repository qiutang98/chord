#pragma once
#include <asset/asset.h>
#include <asset/gltf/gltf_helper.h>

#include <shader/gltf.h>

namespace chord
{
	struct GLTFSampler
	{
		ARCHIVE_DECLARE;

		enum class EMinMagFilter
		{
			NEAREST = 9728,
			LINEAR = 9729,
			NEAREST_MIPMAP_NEAREST = 9984,
			LINEAR_MIPMAP_NEAREST  = 9985,
			NEAREST_MIPMAP_LINEAR  = 9986,
			LINEAR_MIPMAP_LINEAR   = 9987,
		};

		enum class EWrap
		{

			REPEAT = 10497,
			CLAMP_TO_EDGE   = 33071,
			MIRRORED_REPEAT = 33648,
		};

		EMinMagFilter minFilter = EMinMagFilter::NEAREST;
		EMinMagFilter magFilter = EMinMagFilter::NEAREST;
		EWrap wrapS = EWrap::REPEAT;
		EWrap wrapT = EWrap::REPEAT;

		bool operator==(const GLTFSampler&) const = default;

		uint32 getSampler() const;
	};

	struct GLTFTextureInfo
	{
		ARCHIVE_DECLARE;

		AssetSaveInfo image{ };
		int32 textureCoord = 0;
		GLTFSampler sampler { };

		bool isValid() const
		{
			return !image.empty() && !image.isTemp();
		}
	};

	enum class EAlphaMode
	{
		Opaque,
		Mask,
		Blend,
	};
	class GLTFMaterialAsset;

	class GLTFMaterialProxy
	{
	public:
		explicit GLTFMaterialProxy(std::shared_ptr<GLTFMaterialAsset> material);
		virtual ~GLTFMaterialProxy();

		static void init(std::shared_ptr<GLTFMaterialProxy> proxy);

		constexpr static uint32 kGPUSceneDataFloat4Count =
			CHORD_DIVIDE_AND_ROUND_UP(sizeof(GLTFMaterialGPUData), sizeof(float) * 4);

		struct TextureInfo
		{
			bool bExist = false;
			graphics::GPUTextureAssetRef texture = nullptr;

			// Check current texture is ready or not.
			bool isLoadingReady() const
			{
				checkMsgf(texture != nullptr, "Texture must create before check loading state.");
				return texture->isReady();
			}

			uint32 requireSRV(bool bReturnUnValidIfNoExist = false) const;
		};

		TextureInfo baseColorTexture {};
		TextureInfo metallicRoughnessTexture {};
		TextureInfo emissiveTexture {};
		TextureInfo normalTexture {};

		std::shared_ptr<GLTFMaterialAsset> reference = nullptr;

		uint32 getGPUSceneId() const
		{
			return m_gpuSceneGLTFMaterialAssetId;
		}

		void updateGPUScene(bool bForceUpload);

	private:
		void freeGPUScene();


	private:
		const uint64 m_proxyId = 0;
		uint32 m_gpuSceneGLTFMaterialAssetId = -1;
	};
	using GLTFMaterialProxyRef = std::shared_ptr<GLTFMaterialProxy>;

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

		GLTFMaterialProxyRef getProxy();

	protected:
		// ~IAsset virtual function.
		// Call back when call AssetManager::createAsset
		virtual void onPostConstruct() override;

		virtual bool onSave() override;
		virtual void onUnload() override;
		// ~IAsset virtual function.

	private:
		// Weak ref here.
		std::weak_ptr<GLTFMaterialProxy> m_proxy;

	public:
		math::vec4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
		GLTFTextureInfo baseColorTexture;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		GLTFTextureInfo metallicRoughnessTexture;
		GLTFTextureInfo emissiveTexture;
		math::vec3 emissiveFactor = {0.0f, 0.0f, 0.0f};

		EShadingType shadingType = EShadingType::GLTF_MetallicRoughnessPBR;
		EAlphaMode alphaMode = EAlphaMode::Opaque;

		float alphaCoutoff = 0.5f;
		bool bDoubleSided = false;

		GLTFTextureInfo normalTexture;
		float normalTextureScale = 1.0f;
		bool bExistOcclusion = false;
		float occlusionTextureStrength = 1.0f;
	};
	using GLTFMaterialAssetRef = std::shared_ptr<GLTFMaterialAsset>;
	using GLTFMaterialAssetWeak = std::weak_ptr<GLTFMaterialAsset>;

	struct GLTFBVHNode
	{
		ARCHIVE_DECLARE;
		GPUBVHNode data;
	};

	struct GLTFMeshletGroup
	{
		ARCHIVE_DECLARE;
		GPUGLTFMeshletGroup data;
	};

	struct GLTFMeshlet
	{
		ARCHIVE_DECLARE;

		GPUGLTFMeshlet data;
	};
	CHORD_CHECK_SIZE_GPU_SAFE(GLTFMeshlet);

	struct GLTFPrimitive
	{
		ARCHIVE_DECLARE;

		std::string name;

		// Current primitive use which material.
		AssetSaveInfo material;

		//
		uint32 meshletOffset = 0;
		uint32 lod0meshletCount  = 0;
		uint32 bvhNodeCount = 0;
		uint32 meshletGroupCount = 0;

		uint32 bvhNodeOffset = 0;
		uint32 meshletGroupOffset = 0;
		uint32 meshletGroupIndicesOffset = 0;

		uint32 vertexOffset = 0; // used for required attributes.
		uint32 vertexCount  = 0;

		uint32 lod0IndicesOffset = 0;
		uint32 lod0IndicesCount  = 0;
		 
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
	};

	struct GLTFMesh
	{
		ARCHIVE_DECLARE;

		std::string name;
		std::vector<GLTFPrimitive> primitives;
	};

	struct GLTFNode
	{
		ARCHIVE_DECLARE;

		int32 mesh;
		std::string name;
		math::dmat4 localMatrix;
		std::vector<int32> childrenIds;
	};

	struct GLTFScene
	{
		ARCHIVE_DECLARE;

		std::string name;
		std::vector<int32> nodes;
	};

	struct GLTFBinary
	{
		ARCHIVE_DECLARE;

		// Data from primitive accessor.
		struct PrimitiveDatas
		{
			// Meshlet need to push to gpu buffer directly, take care of size pad.
			std::vector<GLTFMeshlet>      meshlets;
			std::vector<uint32>           meshletDatas;
			std::vector<GLTFBVHNode>      bvhNodes;
			std::vector<GLTFMeshletGroup> meshletGroups;
			std::vector<uint32>           meshletGroupIndices;

			// LOD0 indices.
			std::vector<uint32> lod0Indices;

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
					  sizeofV(positions)
					+ sizeofV(normals)
					+ sizeofV(texcoords0)
					+ sizeofV(tangents)
					+ sizeofV(texcoords1)
					+ sizeofV(colors0)
					+ sizeofV(smoothNormals) 
					+ sizeofV(meshlets)
					+ sizeofV(meshletDatas)
					+ sizeofV(bvhNodes)
					+ sizeofV(meshletGroups)
					+ sizeofV(meshletGroupIndices)
					+ sizeofV(lod0Indices);
			}
		} primitiveData;
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

	extern GLTFMaterialAssetRef tryLoadGLTFMaterialAsset(const std::filesystem::path& path, bool bThreadSafe = true);
	inline GLTFMaterialAssetRef tryLoadGLTFMaterialAsset(const AssetSaveInfo& info, bool bThreadSafe = true)
	{
		return tryLoadGLTFMaterialAsset(info.path(), bThreadSafe);
	}
}