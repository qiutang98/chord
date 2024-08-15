#pragma once

#include <utils/utils.h>
#include <asset/meshoptimizer/meshoptimizer.h>
#include <asset/gltf/gltf.h>

namespace chord::nanite
{
	struct Vertex
	{
		float3 position;
		float2 uv0;
		float3 normal;
		float4 tangent;
		float3 smoothNormal;
		float2 uv1;
		float4 color0;
	};
	static_assert(sizeof(Vertex) == 21 * 4);


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
		math::vec3 parentPosCenter;
		math::vec3 clusterPosCenter;

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
			std::vector<Vertex>&& vertices,
			float fuseDistance,
			float coneWeight);

		MeshletContainer build() const;

		const std::vector<Vertex>& getVertices() const { return m_vertices; }

	private:
		void MeshletsGMSS(
			MeshletContainer& srcCtx, 
			MeshletContainer& outCtx, 
			float targetError, 
			uint32 lod) const;

	private:
		const float m_coneWeight;

		// Indices of triangles.
		std::vector<uint32> m_indices;

		// Vertices.
		std::vector<Vertex> m_vertices;
	};
}