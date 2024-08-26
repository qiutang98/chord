#pragma once

#include <asset/asset.h>
#include <asset/texture/texture.h>

#include <scene/scene.h>
#include <scene/component.h>
#include <scene/scene_node.h>
#include <asset/gltf/gltf.h>
#include <scene/component/gltf_mesh.h>
#include <scene/component/gltf_material.h>

registerPODClassMember(GLTFSampler)
{
	ARCHIVE_ENUM_CLASS(minFilter);
	ARCHIVE_ENUM_CLASS(magFilter);
	ARCHIVE_ENUM_CLASS(wrapS);
	ARCHIVE_ENUM_CLASS(minFilter);
}

registerPODClassMember(GLTFTextureInfo)
{
	ar(image, textureCoord, sampler);
}

registerPODClassMember(GLTFBVHNode)
{
	ar(data.sphere);
	ar(data.children);
	ar(data.leafMeshletGroupOffset);
	ar(data.leafMeshletGroupCount);
	ar(data.bvhNodeCount);
}

registerPODClassMember(GLTFMeshletGroup)
{
	ar(data.clusterPosCenter);
	ar(data.parentError);
	ar(data.parentPosCenter);
	ar(data.error);
	ar(data.meshletOffset);
	ar(data.meshletCount);
}

registerPODClassMember(GLTFMeshlet)
{
	ar(data.posMin);
	ar(data.dataOffset);
	ar(data.posMax);
	ar(data.vertexTriangleCount);
	ar(data.coneCutOff);
	ar(data.coneAxis);
	ar(data.coneApex);
	ar(data.lod);
}

registerPODClassMember(GLTFPrimitive)
{
	ar(name, material, vertexCount, vertexOffset);
	ar(bColor0Exist, bSmoothNormalExist, bTextureCoord1Exist);
	ar(posMin, posMax, posAverage, colors0Offset, textureCoord1Offset, smoothNormalOffset);
	ar(meshletOffset, lod0meshletCount, bvhNodeOffset, meshletGroupOffset, meshletGroupIndicesOffset, bvhNodeCount, meshletGroupCount);
}

registerPODClassMember(GLTFMesh)
{
	ar(name, primitives);
}

registerPODClassMember(GLTFNode)
{
	ar(name, childrenIds, localMatrix, mesh);
}

registerPODClassMember(GLTFScene)
{
	ar(name, nodes);
}

registerPODClassMember(GLTFBinary)
{
	ar(primitiveData.positions);
	ar(primitiveData.normals);
	ar(primitiveData.texcoords0);
	ar(primitiveData.tangents);
	ar(primitiveData.smoothNormals);
	ar(primitiveData.texcoords1);
	ar(primitiveData.colors0);
	ar(primitiveData.meshlets);
	ar(primitiveData.meshletDatas);
	ar(primitiveData.bvhNodes);
	ar(primitiveData.meshletGroups);
	ar(primitiveData.meshletGroupIndices);
}

registerClassMember(IAsset)
{
	ar(m_saveInfo, m_rawAssetPath, m_snapshotDimension);
}

registerClassMemberInherit(TextureAsset, IAsset)
{
	ar(m_bSRGB, m_mipmapCount, m_dimension, m_mipmapAlphaCutoff);

	ARCHIVE_ENUM_CLASS(m_format);
	ARCHIVE_ENUM_CLASS(m_colorspace);
}}

registerClassMemberInherit(GLTFMaterialAsset, IAsset)
{
	ar(baseColorFactor);
	ar(baseColorTexture);
	ar(metallicFactor);
	ar(roughnessFactor);
	ar(metallicRoughnessTexture);
	ar(emissiveTexture);
	ar(emissiveFactor);
	ARCHIVE_ENUM_CLASS(alphaMode);
	ar(alphaCoutoff);
	ar(bDoubleSided);
	ar(normalTexture);
	ar(normalTextureScale);
	ar(bExistOcclusion);
	ar(occlusionTextureStrength);
	ARCHIVE_ENUM_CLASS(shadingType);
}}

registerClassMemberInherit(GLTFAsset, IAsset)
{
	ar(m_meshes);
	ar(m_nodes);
	ar(m_defaultScene, m_scenes, m_gltfBinSize);
}}

registerClassMember(Component)
{
	ar(m_node);
}

registerClassMemberInherit(Transform, Component)
{
	ar(m_translation, m_rotation, m_scale);
}}

registerClassMemberInherit(GLTFMeshComponent, Component)
{
	ar(m_gltfMeshId, m_gltfAssetInfo);
}}

registerClassMemberInherit(GLTFMaterialComponent, Component)
{
	ar(m_gltfAssetInfos);
}}

registerClassMember(SceneNode)
{
	ar(m_bVisibility);
	ar(m_bStatic);
	ar(m_id);
	ar(m_name);
	ar(m_parent);
	ar(m_scene);
	ar(m_components);
	ar(m_children);
}

registerClassMemberInherit(Scene, IAsset)
{
	ar(m_currentId);
	ar(m_root);
	ar(m_components);
	ar(m_sceneNodes);
}}

namespace chord
{
	enum class ECompressionMode
	{
		None,
		Lz4,

		MAX
	};

	class AssetCompressedMeta
	{
	public:
		ECompressionMode compressionMode;
		int32 rawSize;
		int32 compressionSize;

		template<class Ar> void serialize(Ar& ar)
		{
			ar(rawSize, compressionSize);
			ARCHIVE_ENUM_CLASS(compressionMode);
		}
	};

	template<typename T>
	static bool saveAsset(const T& in, ECompressionMode compressionMode, const std::filesystem::path& savePath, bool bRequireNoExist = true)
	{
		std::filesystem::path rawSavePath = savePath;
		if (bRequireNoExist && std::filesystem::exists(rawSavePath))
		{
			LOG_ERROR("Meta data {} already exist, make sure never import save resource at same folder!",
				utf8::utf16to8(rawSavePath.u16string()));
			return false;
		}

		std::string rawData;
		{
			std::stringstream ss;
			cereal::BinaryOutputArchive archive(ss);
			archive(in);

			// Exist copy construct.
			rawData = std::move(ss.str());
		}

		AssetCompressedMeta meta;
		meta.compressionMode = compressionMode;
		meta.rawSize = (int32)rawData.size();

		std::string compressedData;
		if (compressionMode == ECompressionMode::Lz4)
		{
			compressedData.resize(LZ4_compressBound((int32)rawData.size()));
			meta.compressionSize = LZ4_compress_default(
				rawData.c_str(),
				compressedData.data(),
				(int32)rawData.size(),
				(int32)compressedData.size());

			compressedData.resize(meta.compressionSize);
		}
		else if (meta.compressionMode == ECompressionMode::None)
		{
			meta.compressionSize = meta.rawSize;
			compressedData = std::move(rawData);
		}
		else
		{
			checkEntry();
		}
		check(compressedData.size() == meta.compressionSize);

		{
			std::ofstream os(rawSavePath, std::ios::binary);
			cereal::BinaryOutputArchive archive(os);
			archive(meta, compressedData);
		}
		return true;
	}

	template<typename T>
	static bool loadAsset(T& out, const std::filesystem::path& savePath)
	{
		if (!std::filesystem::exists(savePath))
		{
			LOG_ERROR("Asset data {} miss!", utf8::utf16to8(savePath.u16string()));
			return false;
		}

		AssetCompressedMeta meta;
		std::string compressedData;
		{
			std::ifstream is(savePath, std::ios::binary);
			cereal::BinaryInputArchive archive(is);
			archive(meta, compressedData);

			check(meta.compressionSize == compressedData.size());
		}

		// Allocate raw data memory.
		std::string rawData;
		if (meta.compressionMode == ECompressionMode::Lz4)
		{
			rawData.resize(meta.rawSize);

			const int32 rawSize = LZ4_decompress_safe(compressedData.data(), rawData.data(), meta.compressionSize, meta.rawSize);
			check(rawSize == meta.rawSize);
		}
		else if (meta.compressionMode == ECompressionMode::None)
		{
			// Just move compression data to raw data.
			rawData = std::move(compressedData);
			check(meta.compressionSize == meta.rawSize);
		}
		else
		{
			checkEntry();
		}

		{
			std::stringstream ss;
			// Exist copy-construct.
			ss << std::move(rawData);
			cereal::BinaryInputArchive archive(ss);
			archive(out);
		}

		return true;
	}
}