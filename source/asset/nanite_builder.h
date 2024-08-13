#pragma once

#include <utils/utils.h>
#include <asset/meshoptimizer/meshoptimizer.h>
#include <asset/gltf/gltf.h>

namespace chord::nanite
{
	struct Meshlet
	{
		meshopt_Meshlet info;

		float      coneCutOff;
		math::vec3 coneAxis;
		math::vec3 coneApex;

		// Position min and pos max.
		math::vec3 posMin;
		math::vec3 posMax;

		// LOD index of current meshlet.
		uint32 lod;

		// LOD reduce relative mesh space error.
		float error = 0.0f;
		float parentError = std::numeric_limits<float>::max();

		// Bounds sphere.
		math::vec4 boundingSphere;
		math::vec4 parentBoundingSphere;

		bool isParentSet() const;

		GLTFMeshlet getGLTFMeshlet(uint32 dataOffset) const;
	};

	struct MeshletContainer
	{
		// Indices info.
		std::vector<uint8> triangles;
		std::vector<uint32> vertices;

		// 
		std::vector<Meshlet> meshlets;

		void merge(MeshletContainer&& rhs);
	};

	class NaniteBuilder
	{
	public:
		explicit NaniteBuilder(
			std::vector<uint32>&& indices, 
			const std::vector<math::vec3>& positions,
			float coneWeight);

		MeshletContainer build() const;

		const std::vector<uint32>& getLod0Indices() const
		{
			return m_indicesLod0;
		}

	private:
		void MeshletsGMSS(
			MeshletContainer& srcCtx, 
			MeshletContainer& outCtx, 
			float targetError, 
			uint32 lod,
			float simplifyScale) const;

	private:
		const float m_coneWeight;

		// Indices of triangles.
		std::vector<uint32> m_indicesLod0;

		// Reference positions.
		const std::vector<math::vec3>& m_positions;
	};
}