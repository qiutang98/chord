#pragma once 

#include "base.h"

namespace chord
{
	struct gltfTexture
	{
		int index;
		int textureCoord;
	};

	struct KHR_materials_specular
	{
		float3 specularColorFactor;
		float specularFactor;

		gltfTexture specularTexture;
		gltfTexture specularColorTexture;
	};

	struct KHR_texture_transform
	{
		float2 offset;
		float2 scale;

		mat3 uvTransform;
		float rotation;
		int texCoord;
	};

	struct KHR_materials_clearcoat
	{
		float factor;
		gltfTexture   texture;
		float roughnessFactor;
		int   roughnessTexture;

		int   normalTexture;
	};

	struct KHR_materials_sheen
	{
		float3 colorFactor;
		int       colorTexture;

		float     roughnessFactor;
		int       roughnessTexture;
	};

	struct KHR_materials_transmission
	{
		float factor;
		int texture;
	};

	struct KHR_materials_unlit
	{
		int active;
	};

	struct KHR_materials_anisotropy
	{
		float factor;
		glm::vec3 direction;
		int texture;
	};

	struct gltfMaterial
	{
		float4 baseColorFactor;

		int baseColorTexture;
		float metallicFactor;
		float roughnessFactor;
		int metallicRoughnessTexture;

		float3 emissiveFactor;
		int emissiveTexture;

		int alphaMode;
		float alphaCutoff;
		int doubleSided;
		int normalTexture;

		float normalTextureScale;
		int occlusionTexture;
		float occlusionTextureStrength;

		KHR_materials_specular 
	};
}