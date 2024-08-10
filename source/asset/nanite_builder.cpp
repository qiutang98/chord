#include <asset/nanite_builder.h>

#include <metis/metis.h>
#include <shader/base.h>
#include <utils/log.h>

#include <asset/meshoptimizer/meshoptimizer.h>

using namespace chord;
using namespace chord::nanite;

struct Meshlet
{
	meshopt_Meshlet info;
	meshopt_Bounds bounds;
	math::vec3 posMin;
	math::vec3 posMax;
};

struct MeshletContainer
{
	// Indices info.
	std::vector<uint8> triangles;
	std::vector<uint32> vertices;

	// 
	std::vector<Meshlet> meshlets;
};

static MeshletContainer buildMeshlets(const math::vec3* positions, const uint32 verticesCount, const std::vector<uint32>& indices, float coneWeight)
{
	MeshletContainer result{ };

	std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(indices.size(), kNaniteMeshletMaxVertices, kNaniteMeshletMaxTriangle));
	{
		std::vector<uint32> meshletVertices(meshlets.size() * kNaniteMeshletMaxVertices);
		std::vector<uint8> meshletTriangles(meshlets.size() * kNaniteMeshletMaxTriangle * 3);

		// Build meshlet.
		meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), indices.data(), indices.size(), &positions[0].x, verticesCount, sizeof(positions[0]), kNaniteMeshletMaxVertices, kNaniteMeshletMaxTriangle, coneWeight));

		// Move data to result.
		result.triangles = std::move(meshletTriangles);
		result.vertices  = std::move(meshletVertices);
	}

	// 
	result.meshlets = { };
	result.meshlets.reserve(meshlets.size());

	// For-each meshlet, compute bounds.
	for (const auto& meshlet : meshlets)
	{
		meshopt_optimizeMeshlet(&result.vertices[meshlet.vertex_offset], &result.triangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);
		meshopt_Bounds bounds = meshopt_computeMeshletBounds(&result.vertices[meshlet.vertex_offset], &result.triangles[meshlet.triangle_offset], meshlet.triangle_count, &positions[0].x, verticesCount, sizeof(positions[0]));
	
		math::vec3 posMin = math::vec3( FLT_MAX);
		math::vec3 posMax = math::vec3(-FLT_MAX);

		for (uint32 triangleId = 0; triangleId < meshlet.triangle_count; triangleId++)
		{
			uint8 id0 = result.triangles[meshlet.triangle_offset + triangleId * 3 + 0];
			uint8 id1 = result.triangles[meshlet.triangle_offset + triangleId * 3 + 1];
			uint8 id2 = result.triangles[meshlet.triangle_offset + triangleId * 3 + 2];

			uint32 index0 = result.vertices[meshlet.vertex_offset + id0];
			uint32 index1 = result.vertices[meshlet.vertex_offset + id1];
			uint32 index2 = result.vertices[meshlet.vertex_offset + id2];

			posMax = math::max(posMax, positions[index0]);
			posMax = math::max(posMax, positions[index1]);
			posMax = math::max(posMax, positions[index2]);

			posMin = math::min(posMin, positions[index0]);
			posMin = math::min(posMin, positions[index1]);
			posMin = math::min(posMin, positions[index2]);
		}

		// Add new one meshlet.
		Meshlet newMeshlet { };
		newMeshlet.info = meshlet;
		newMeshlet.bounds = bounds;
		newMeshlet.posMin = posMin;
		newMeshlet.posMax = posMax;
		result.meshlets.push_back(newMeshlet);
	}

	return result;
}

NaniteBuilder::NaniteBuilder(std::vector<uint32>&& indices, uint32 verticesCount, const math::vec3* positions)
	: m_indices(indices)
	, m_positions(positions)
	, m_verticesCount(verticesCount)
{
	checkMsgf(m_indices.size() % 3 == 0, "Nanite only support triangle mesh!");

	meshopt_optimizeVertexCache(m_indices.data(), m_indices.data(), m_indices.size(), m_verticesCount);
}


