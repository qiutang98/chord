#include <asset/nanite_builder.h>

#include <metis/metis.h>
#include <shader/base.h>
#include <utils/log.h>

using namespace chord;
using namespace chord::nanite;

constexpr auto kNumMeshletPerGroup = 4;
constexpr auto kNumGroupSplitAfterSimplify = 2;
constexpr auto kGroupSimplifyThreshold = 1.0f / float(kNumGroupSplitAfterSimplify);
constexpr auto kGroupSimplifyMinReduce = 0.8f; // At least next level lod need to reduce 20% triangles, otherwise it is no meaning to store it.

constexpr auto kSimplifyError = 0.01f;
constexpr auto kGroupMergePosError = 0.1f;

using MeshletIndex = uint32;
using VertexIndex  = uint32;
using ClusterGroup = std::vector<MeshletIndex>;

constexpr float kMeshletParentErrorUninitialized = std::numeric_limits<float>::max();

uint32 loadVertexIndex(
	const MeshletContainer& ctx, 
	const meshopt_Meshlet& meshlet, 
	uint32 triangleId, 
	uint32 vertexId)
{
	uint8 id = ctx.triangles[meshlet.triangle_offset + triangleId * 3 + vertexId];
	return ctx.vertices[id + meshlet.vertex_offset];
}

static MeshletContainer buildMeshlets(
	const std::vector<Vertex>& vertices,
	const std::vector<uint32>& indices, 
	float coneWeight, 
	uint lod, 
	float error,
	float3 clusterPosCenter)
{
	MeshletContainer result{ };

	const uint32 verticesCount = vertices.size();
	std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(indices.size(), kNaniteMeshletMaxVertices, kNaniteMeshletMaxTriangle));
	{
		std::vector<uint32> meshletVertices(meshlets.size() * kNaniteMeshletMaxVertices);
		std::vector<uint8> meshletTriangles(meshlets.size() * kNaniteMeshletMaxTriangle * 3);

		// Build meshlet.
		meshlets.resize(meshopt_buildMeshlets(
			meshlets.data(),
			meshletVertices.data(),
			meshletTriangles.data(),
			indices.data(),
			indices.size(),
			&vertices[0].position.x,
			verticesCount,
			sizeof(vertices[0]),
			kNaniteMeshletMaxVertices,
			kNaniteMeshletMaxTriangle,
			coneWeight));

		// Move data to result.
		result.triangles = std::move(meshletTriangles);
		result.vertices = std::move(meshletVertices);
	}

	// 
	result.meshlets = { };
	result.meshlets.reserve(meshlets.size());

	// For-each meshlet, compute bounds.
	for (const auto& meshlet : meshlets)
	{
		meshopt_optimizeMeshlet(
			&result.vertices[meshlet.vertex_offset],
			&result.triangles[meshlet.triangle_offset],
			meshlet.triangle_count,
			meshlet.vertex_count);

		meshopt_Bounds bounds = meshopt_computeMeshletBounds(
			&result.vertices[meshlet.vertex_offset],
			&result.triangles[meshlet.triangle_offset],
			meshlet.triangle_count,
			&vertices[0].position.x,
			verticesCount,
			sizeof(vertices[0]));

		math::vec3 posMin = math::vec3( FLT_MAX);
		math::vec3 posMax = math::vec3(-FLT_MAX);

		check(meshlet.triangle_count < 256);
		check(meshlet.vertex_count   < 256);

		for (uint32 triangleId = 0; triangleId < meshlet.triangle_count; triangleId++)
		{
			for (uint i = 0; i < 3; i++)
			{
				VertexIndex vid = loadVertexIndex(result, meshlet, triangleId, i);
				posMax = math::max(posMax, vertices[vid].position);
				posMin = math::min(posMin, vertices[vid].position);
			}
		}

		const float3 posCenter = 0.5f * (posMax + posMin);
		const float radius = math::length(posMax - posCenter);

		// Add new one meshlet.
		Meshlet newMeshlet{ };

		// Set parent uninitialized.
		newMeshlet.error = error;
		newMeshlet.parentError = kMeshletParentErrorUninitialized;

		newMeshlet.info = meshlet;
		newMeshlet.lod = lod;

		// 
		newMeshlet.posMin = posMin;
		newMeshlet.posMax = posMax;

		// Cone culling.
		newMeshlet.coneCutOff = bounds.cone_cutoff;
		newMeshlet.coneAxis = math::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]);
		newMeshlet.coneApex = math::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]);

		// Current meshlet bouding sphere.
		newMeshlet.clusterPosCenter = clusterPosCenter;
		newMeshlet.parentPosCenter  = clusterPosCenter;


		// 
		result.meshlets.push_back(newMeshlet);
	}

	return result;
}

struct MeshletEdge 
{
	explicit MeshletEdge(VertexIndex a, VertexIndex b)
		: first(math::min(a, b))
		, second(math::max(a, b))
	{
		
	}

	struct Hash 
	{
		std::size_t operator()(const MeshletEdge& edge) const 
		{
			static_assert(std::is_same_v<std::size_t, uint64>);
			return (uint64(edge.second) << 32) | uint64(edge.first);
		}
	};

	bool operator==(const MeshletEdge& other) const = default;

	const VertexIndex first;
	const VertexIndex second;
};

static inline std::vector<ClusterGroup> buildOneClusterGroup(const MeshletContainer& ctx)
{
	ClusterGroup result(ctx.meshlets.size());
	for (MeshletIndex i = 0; i < ctx.meshlets.size(); i++)
	{
		result[i] = i;
	}

	return { result };
}

uint32 hashPosition(float3 pos, float posFuseThreshold)
{
	int3 posInt3 = math::ceil(pos / posFuseThreshold);
	return crc::crc32((void*)&posInt3, sizeof(posInt3), 0);
}

 bool buildClusterGroup(const MeshletContainer& ctx, std::vector<ClusterGroup>& outGroup, const std::vector<Vertex>& vertices, float posFuseThreshold)
{
	const auto& meshlets = ctx.meshlets;

	if (meshlets.size() < kNumGroupSplitAfterSimplify * kNumMeshletPerGroup)
	{
		// No enough meshlet count to group-simplify-split, just return one group.
		outGroup = buildOneClusterGroup(ctx);
		return false;
	}

	std::unordered_map<MeshletEdge,  std::unordered_set<MeshletIndex>, MeshletEdge::Hash>  edges2Meshlets;
	std::unordered_map<MeshletIndex, std::unordered_set<MeshletEdge,   MeshletEdge::Hash>> meshlets2Edges;
	{
		// Loop all meshlet build indexing map.
		for (MeshletIndex meshletIndex = 0; meshletIndex < meshlets.size(); meshletIndex++)
		{
			const auto& meshlet = meshlets[meshletIndex];
			const auto& meshletInfo = meshlet.info;

			// for each triangle of the meshlet
			for (uint32 triangleIndex = 0; triangleIndex < meshletInfo.triangle_count; triangleIndex++)
			{
				// for each edge of the triangle 
				for (uint32 i = 0; i < 3; i++) // 0-1 1-2 2-0
				{
					VertexIndex v0 = loadVertexIndex(ctx, meshletInfo, triangleIndex, i);
					VertexIndex v1 = loadVertexIndex(ctx, meshletInfo, triangleIndex, (i + 1) % 3);

					MeshletEdge edge(hashPosition(vertices.at(v0).position, posFuseThreshold), hashPosition(vertices.at(v1).position, posFuseThreshold));
					// (edge.first != edge.second);
					{
						edges2Meshlets[edge].insert(meshletIndex);
						meshlets2Edges[meshletIndex].insert(edge);
					}
				}
			}
		}
	}

	// Remove edges which are not connected to 2 different meshlets.
	std::erase_if(edges2Meshlets, [&](const auto& pair) { return pair.second.size() <= 1; });

	if (edges2Meshlets.empty()) 
	{
		// No connected meshlet exist, return one group.
		outGroup = buildOneClusterGroup(ctx);
		return false;
	}

	// Metis group partition.
	{
		// Vertex count, from the point of view of METIS, where Meshlet = vertex
		idx_t vertexCount = meshlets.size();

		std::vector<idx_t> xadjacency;
		xadjacency.reserve(vertexCount + 1);

		std::vector<idx_t> edgeAdjacency { };
		std::vector<idx_t> edgeWeights { };
		for (MeshletIndex meshletIndex = 0; meshletIndex < meshlets.size(); meshletIndex++) 
		{
			size_t edgeAdjOffset = edgeAdjacency.size();
			xadjacency.push_back(edgeAdjOffset);

			for (const auto& edge : meshlets2Edges[meshletIndex]) 
			{
				auto connectionsIter = edges2Meshlets.find(edge);
				if (connectionsIter == edges2Meshlets.end()) 
				{
					// This edge no connected any meshlet.
					continue;
				}

				const auto& connections = connectionsIter->second;
				for (const auto& connectedMeshlet : connections) 
				{
					// Only use other meshlet.
					if (connectedMeshlet != meshletIndex) 
					{
						auto existingEdgeIter = std::find(edgeAdjacency.begin() + edgeAdjOffset, edgeAdjacency.end(), connectedMeshlet);
						if (existingEdgeIter == edgeAdjacency.end())
						{
							checkMsgf(edgeAdjacency.size() == edgeWeights.size(),
								"edgeWeights and edgeAdjacency must have the same length.");

							edgeAdjacency.push_back(connectedMeshlet);
							edgeWeights.push_back(1);
						}
						else 
						{
							std::ptrdiff_t d = existingEdgeIter - edgeAdjacency.begin();
							checkMsgf(d >= 0 && d < edgeWeights.size(), 
								"edgeWeights and edgeAdjacency do not have the same length.");

							// More than one meshlet conneted, edge weight plus.
							edgeWeights[d]++;
						}
					}
				}
			}
		}
		xadjacency.push_back(edgeAdjacency.size());
		checkMsgf(xadjacency.size() == vertexCount + 1, "unexpected count of vertices for METIS graph.");

		idx_t options[METIS_NOPTIONS];
		METIS_SetDefaultOptions(options);
		options[METIS_OPTION_OBJTYPE]   = METIS_OBJTYPE_CUT;
		options[METIS_OPTION_CCORDER]   = 1; // identify connected components first
		options[METIS_OPTION_NUMBERING] = 0;

		idx_t edgeCut;
		idx_t ncon = 1;
		idx_t nparts = meshlets.size() / kNumMeshletPerGroup;
		std::vector<idx_t> partition(vertexCount);
		int metisPartResult = METIS_PartGraphKway(&vertexCount,
			&ncon,
			xadjacency.data(),
			edgeAdjacency.data(),
			nullptr, /* vertex weights */
			nullptr, /* vertex size */
			edgeWeights.data(),
			&nparts,
			nullptr,
			nullptr,
			options,
			&edgeCut,
			partition.data()
		);
		checkMsgf(metisPartResult == METIS_OK, "Graph partitioning failed!");

		outGroup.resize(nparts);
		for (std::size_t i = 0; i < meshlets.size(); i++) 
		{
			idx_t partitionNumber = partition[i];
			outGroup[partitionNumber].push_back(i);
		}
	}

	return true;
}

// Input one lod level meshlet container, do Group-Merge-Simplify-Split operation.
// Return true if success, return false if stop.
void NaniteBuilder::MeshletsGMSS(
	MeshletContainer& srcCtx,
	MeshletContainer& outCtx, 
	float targetError, 
	uint32 lod) const
{
	// Group.
	std::vector<ClusterGroup> clusterGroups;
	const float posFuseThreshold = targetError * kGroupMergePosError;

	bool bCanGroup = buildClusterGroup(srcCtx, clusterGroups, m_vertices, posFuseThreshold);

	if (bCanGroup)
	{
		// It never should be empty.
		check(!clusterGroups.empty());
	}
	else
	{
		return;
	}

	// Merge-Simplify-Split.
	for (const auto& clusterGroup : clusterGroups)
	{
		std::vector<uint8> verticesLocked(m_vertices.size());
		memset(verticesLocked.data(), 0, verticesLocked.size() * sizeof(verticesLocked[0]));

		std::unordered_map<MeshletEdge, uint32, MeshletEdge::Hash> edgeUsedCount;

		// Merge meshlet in one group.
		std::vector<VertexIndex> groupMergeVertices;
		{
			for (uint32 meshletIndex : clusterGroup)
			{
				const auto& meshlet = srcCtx.meshlets[meshletIndex];
				for (uint triangleId = 0; triangleId < meshlet.info.triangle_count; triangleId++)
				{
					for (uint i = 0; i < 3; i++)
					{
						VertexIndex v0 = loadVertexIndex(srcCtx, meshlet.info, triangleId, i);
						groupMergeVertices.push_back(v0);



						VertexIndex v1 = loadVertexIndex(srcCtx, meshlet.info, triangleId, (i + 1) % 3);
						MeshletEdge edge(v0, v1);
						edgeUsedCount[edge] ++;
					}
				}
			}
		}

		uint32 edgeBorderCount = 0;
		for (const auto& pair : edgeUsedCount)
		{
			if (pair.second == 1) // is edge.
			{
				MeshletEdge edge = pair.first;
				verticesLocked[edge.first]  = 1;
				verticesLocked[edge.second] = 1;

				edgeBorderCount++;
			}
		}

		if (edgeBorderCount == edgeUsedCount.size())
		{
			LOG_ERROR("The loose geometry import, please check fuse when import mesh...");
			continue;
		}

		// Simplify group.
		std::vector<VertexIndex> simplifiedVertices(groupMergeVertices.size());
		float simplificationError = 0.f;
		{
			const auto targetIndicesCount = groupMergeVertices.size() * kGroupSimplifyThreshold;
			uint32 options = meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute;

			simplifiedVertices.resize(meshopt_simplifyWithAttributes(
				simplifiedVertices.data(),
				groupMergeVertices.data(),
				groupMergeVertices.size(),
				&m_vertices[0].position.x,
				m_vertices.size(),
				sizeof(m_vertices[0]),
				NULL,
				0,
				NULL,
				0,
				verticesLocked.data(),
				targetIndicesCount,
				targetError,
				options,
				&simplificationError));
		}

		// Split - half meshlet.
		const auto targetIndicesMin = groupMergeVertices.size() * kGroupSimplifyMinReduce;
		if (simplifiedVertices.size() > 0 && simplifiedVertices.size() < targetIndicesMin)
		{
			float passedError = 0.0f;
			for (uint32 sMid : clusterGroup)
			{
				passedError = math::max(passedError, srcCtx.meshlets[sMid].error);
			}
			const float clusterError = simplificationError + passedError;
			{
				float3 posMin = math::vec3( FLT_MAX);
				float3 posMax = math::vec3(-FLT_MAX);

				for (auto vid : simplifiedVertices)
				{
					posMax = math::max(posMax, m_vertices[vid].position);
					posMin = math::min(posMin, m_vertices[vid].position);
				}

				const float3 clusterPosCenter = 0.5f * (posMax + posMin);
				// Fill parent infos.
				for (uint32 sMid : clusterGroup)
				{
					srcCtx.meshlets[sMid].parentError = clusterError;
					srcCtx.meshlets[sMid].parentPosCenter = clusterPosCenter;
				}

				// Split new meshlets, and fill in return ctx.
				MeshletContainer nextMeshletCtx = buildMeshlets(m_vertices, simplifiedVertices, m_coneWeight, lod, clusterError, clusterPosCenter);
				outCtx.merge(std::move(nextMeshletCtx));
			}
		}
	}
}

MeshletContainer NaniteBuilder::build() const
{
	MeshletContainer finalCtx { };
	checkMsgf(m_indices.size() % 3 == 0, "Nanite only support triangle mesh!");

	// Compute simplify scale for later cluster simplify.
	const float meshOptSimplifyScale = meshopt_simplifyScale(&m_vertices[0].position.x, m_vertices.size(), sizeof(m_vertices[0]));

	// Src mesh input meshlet context.
	MeshletContainer currentLodCtx = buildMeshlets(m_vertices, m_indices, m_coneWeight, 0, 0.0f, math::vec3(0.0f));

	//
	MeshletContainer nextLodCtx = { };
	for (uint32 lod = 0; lod < (kNaniteMaxLODCount - 1); lod++)
	{
		// Clear next lod ctx.
		nextLodCtx = {};

		MeshletsGMSS(currentLodCtx, nextLodCtx, kSimplifyError * meshOptSimplifyScale, lod + 1);

		// Current lod ctx already update parent data, so merge to final.
		finalCtx.merge(std::move(currentLodCtx));

		if (nextLodCtx.meshlets.empty())
		{
			break;
		}

		// Swap for next lod.
		currentLodCtx = std::move(nextLodCtx);
	}

	return finalCtx;
}

struct FuseVertex
{
	int3 hashPos;
	Vertex averageVertex;
};

NaniteBuilder::NaniteBuilder(
	std::vector<uint32>&& inputIndices,
	std::vector<Vertex>&& inputVertices,
	float fuseDistance,
	float coneWeight)
	: m_indices(inputIndices)
	, m_vertices(inputVertices)
	, m_coneWeight(coneWeight)
{


	const bool bFuse = fuseDistance > 0.0f;
	if (bFuse)
	{

	}


	size_t indexCount = m_indices.size();

	std::vector<uint32> remap(m_indices.size()); // allocate temporary memory for the remap table
	size_t vertexCount = meshopt_generateVertexRemap(&remap[0], 
		m_indices.data(), indexCount, &m_vertices[0], indexCount, sizeof(Vertex));

	std::vector<Vertex> remapVertices(vertexCount);
	std::vector<uint32> remapIndices(indexCount);

	meshopt_remapVertexBuffer(remapVertices.data(), m_vertices.data(), indexCount, sizeof(Vertex), remap.data());
	meshopt_remapIndexBuffer(remapIndices.data(), m_indices.data(), indexCount, remap.data());

	meshopt_optimizeVertexCache(remapIndices.data(), remapIndices.data(), indexCount, vertexCount);
	meshopt_optimizeVertexFetch(remapVertices.data(), remapIndices.data(), indexCount, remapVertices.data(), vertexCount, sizeof(Vertex));

	m_indices = std::move(remapIndices);
	m_vertices = std::move(remapVertices);
}

bool Meshlet::isParentSet() const
{
	return parentError != kMeshletParentErrorUninitialized;
}

GLTFMeshlet Meshlet::getGLTFMeshlet(uint32 dataOffset) const
{
	GLTFMeshlet m = { };

	// offset.
	m.data.vertexTriangleCount = packVertexCountTriangleCount(info.vertex_count, info.triangle_count);
	m.data.dataOffset = dataOffset;

	// bounds.
	m.data.posMin = posMin;
	m.data.posMax = posMax;

	// cone.
	m.data.coneAxis   = coneAxis;
	m.data.coneApex   = coneApex;
	m.data.coneCutOff = coneCutOff;

	m.data.lod = lod;
	m.data.error = error;

	m.data.parentError = parentError;
	m.data.parentPosCenter = parentPosCenter;
	m.data.clusterPosCenter = clusterPosCenter;

	return m;
}


void MeshletContainer::merge(MeshletContainer&& inRhs)
{
	MeshletContainer rhs = inRhs;

	uint32 baseTriangleOffset = triangles.size();
	uint32 baseVerticesOffset = vertices.size();

	for (auto& meshlet : rhs.meshlets)
	{
		meshlet.info.triangle_offset += baseTriangleOffset;
		meshlet.info.vertex_offset   += baseVerticesOffset;
	}

	triangles.insert(triangles.end(), rhs.triangles.begin(), rhs.triangles.end());
	vertices.insert(vertices.end(), rhs.vertices.begin(), rhs.vertices.end());
	meshlets.insert(meshlets.end(), rhs.meshlets.begin(), rhs.meshlets.end());
}
