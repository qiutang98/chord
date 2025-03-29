#include <asset/nanite_builder.h>

#include <metis/metis.h>
#include <shader/base.h>
#include <utils/log.h>
#include <utils/cityhash.h>

#include <shader/instance_culling.hlsl>
#include <asset/mikktspace.h>

namespace chord::nanite
{ 

// Group-merge-simplify-split parameters.
constexpr uint32 kMinNumMeshletPerGroup = 2;
constexpr uint32 kMaxNumMeshletPerGroup = 4;
constexpr uint32 kNumGroupSplitAfterSimplify = 2;
constexpr float  kGroupSimplifyThreshold = 1.0f / float(kNumGroupSplitAfterSimplify);

// At least next level lod need to reduce 20% triangles, otherwise it is no meaning to store it.
constexpr auto kGroupSimplifyMinReduce = 0.8f; 

// Simplify error relative to extent.
constexpr auto kSimplifyErrorMin = 0.01f;
constexpr auto kSimplifyErrorMax = 0.10f;

// Group merge position error, relative to simplify error. 
constexpr auto kGroupMergePosError = 0.1f;    

using MeshletIndex = uint32;
using VertexIndex = uint32;

struct ClusterGroup
{
	std::vector<MeshletIndex> meshletIndices;

	float3 clusterPosCenter;
	float parentError;

	float3 parentPosCenter;
	float error;
};

// Load all error sphere, generate bvh accelerate iteration.
class ClusterParentErrorBVHTree
{
public:
	struct Node
	{
		float3 minPos;
		float3 maxPos;

		float4 sphereBuild() const
		{
			return float4(0.5f * (maxPos + minPos), 0.5f * math::length(maxPos - minPos));
		}

		std::array<std::unique_ptr<Node>, kNaniteBVHLevelNodeCount> children;
		std::vector<ClusterGroup> leaves;

		uint flattenIndex = kUnvalidIdUint32;
		uint depth = 0;
	};

	std::unique_ptr<Node> root;
};

static uint64 hashMeshletGroup(const Meshlet& meshlet)
{
	float4 errors[2];
	errors[0] = float4(meshlet.clusterPosCenter, meshlet.error);
	errors[1] = float4(meshlet.parentPosCenter, meshlet.parentError);

	return cityhash::cityhash64((const char*)errors, sizeof(float4) * 2);
}

static void buildBVH(const MeshletContainer& ctx, ClusterParentErrorBVHTree::Node& rootNode, std::vector<const ClusterGroup*>&& rootNodeClusterGroupInBounds)
{
	struct NodeAndClusterGroups
	{
		std::vector<const ClusterGroup*> clusterGroupInBounds;
		ClusterParentErrorBVHTree::Node* node;
	};

	std::queue<NodeAndClusterGroups> nodeQueue { };
	nodeQueue.push( {.clusterGroupInBounds  = std::move(rootNodeClusterGroupInBounds), .node = &rootNode});

	while (!nodeQueue.empty())
	{
		auto* currentNodePtr = nodeQueue.front().node;
		std::vector<const ClusterGroup*> clusterGroupInBounds = std::move(nodeQueue.front().clusterGroupInBounds);

		// Pop current node.
		nodeQueue.pop();

		if (clusterGroupInBounds.empty())
		{
			continue;
		}

		// No enough node to split, build leaf.
		if (clusterGroupInBounds.size() < kNaniteBVHLevelNodeCount || (currentNodePtr->depth == (kNaniteMaxBVHLevelCount - 1)))
		{
			for (const auto& clusterPtr : clusterGroupInBounds)
			{
				currentNodePtr->leaves.push_back(*clusterPtr);
			}
			continue;
		}

		auto getLongestAxisIndex = [](float3 minPos, float3 maxPos)
		{
			uint longestAxis = 0;
			{
				float3 max2min = maxPos - minPos;
				if (max2min.y >= max2min.x && max2min.y >= max2min.z) { longestAxis = 1; } // y axis
				if (max2min.z >= max2min.x && max2min.z >= max2min.y) { longestAxis = 2; } // z axis
			}
			return longestAxis;
		};

		auto getLongestAxisSortGroups = [](uint longestAxis, std::vector<const ClusterGroup*>&& inGroupInBound)
		{
			std::vector<const ClusterGroup*> groupsInBound = inGroupInBound;

			// Sort group on longest axis.
			std::vector<std::pair<const ClusterGroup*, float>> meshletCenterInLongestAxisSort;
			for (const auto* groupPtr : groupsInBound)
			{
				meshletCenterInLongestAxisSort.push_back({ groupPtr, groupPtr->parentPosCenter[longestAxis] });
			}
			std::ranges::sort(meshletCenterInLongestAxisSort, [](const auto& a, const auto& b) { return a.second < b.second; });

			//
			return std::move(meshletCenterInLongestAxisSort);
		};

		// Split 2x2x2
		static_assert(kNaniteBVHLevelNodeCount == 8);
		{
			const uint longest_0   = getLongestAxisIndex(currentNodePtr->minPos, currentNodePtr->maxPos);
			const auto sort_0      = getLongestAxisSortGroups(longest_0, std::move(clusterGroupInBounds));
			const uint sort_0_size = sort_0.size();

			// split half
			for (uint i = 0; i < 2; i++)
			{
				float3 posMin_0 = float3( FLT_MAX);
				float3 posMax_0 = float3(-FLT_MAX);
				std::vector<const ClusterGroup*> iterGroups_0{ };
				for (uint gid = i * sort_0_size / 2; gid < (i + 1) * sort_0_size / 2; gid ++)
				{
					const auto& group = *(sort_0[gid].first);

					posMin_0 = math::min(posMin_0, group.parentPosCenter - group.parentError);
					posMax_0 = math::max(posMax_0, group.parentPosCenter + group.parentError);
					iterGroups_0.push_back(&group);
				}

				const uint longest_1   = getLongestAxisIndex(posMin_0, posMax_0);
				const auto sort_1      = getLongestAxisSortGroups(longest_1, std::move(iterGroups_0));
				const uint sort_1_size = sort_1.size();

				for (uint j = 0; j < 2; j++)
				{
					float3 posMin_1 = float3( FLT_MAX);
					float3 posMax_1 = float3(-FLT_MAX);
					std::vector<const ClusterGroup*> iterGroups_1{ };
					for (uint gid = j * sort_1_size / 2; gid < (j + 1) * sort_1_size / 2; gid++)
					{
						const auto& group = *(sort_1[gid].first);

						posMin_1 = math::min(posMin_1, group.parentPosCenter - group.parentError);
						posMax_1 = math::max(posMax_1, group.parentPosCenter + group.parentError);
						iterGroups_1.push_back(&group);
					}

					const uint longest_2   = getLongestAxisIndex(posMin_1, posMax_1);
					const auto sort_2      = getLongestAxisSortGroups(longest_2, std::move(iterGroups_1));
					const uint sort_2_size = sort_2.size();

					for (uint k = 0; k < 2; k++)
					{
						float3 posMin_2 = float3( FLT_MAX);
						float3 posMax_2 = float3(-FLT_MAX);
						std::vector<const ClusterGroup*> iterGroups_2 { };

						for (uint gid = k * sort_2_size / 2; gid < (k + 1) * sort_2_size / 2; gid++)
						{
							const auto& group = *(sort_2[gid].first);

							posMin_2 = math::min(posMin_2, group.parentPosCenter - group.parentError);
							posMax_2 = math::max(posMax_2, group.parentPosCenter + group.parentError);
							iterGroups_2.push_back(&group);
						}

						uint nodeId = (i * 2 + j) * 2 + k;

						auto& child = currentNodePtr->children[nodeId];
						child = std::make_unique<ClusterParentErrorBVHTree::Node>();
						child->depth = currentNodePtr->depth + 1; // Depth add one.
						child->maxPos = posMax_2;
						child->minPos = posMin_2;

						// Recursive build.
						nodeQueue.push({ .clusterGroupInBounds = std::move(iterGroups_2), .node = child.get() });
					}
				}
			}
		}

	}
}

static void flattenBVH(
	const MeshletContainer& ctx, 
	ClusterParentErrorBVHTree::Node& root, 
	std::vector<GPUGLTFMeshletGroup>& meshletGroups,
	std::vector<GPUBVHNode>& flattenNode, 
	std::vector<uint32>& meshletGroupIndices)
{
	std::queue<ClusterParentErrorBVHTree::Node*> nodeQueue;

	nodeQueue.push(&root);
	while (!nodeQueue.empty())
	{
		auto* node = nodeQueue.front();
		{
			const uint currentIndex = flattenNode.size();
			flattenNode.push_back({ });

			auto& currentNode = flattenNode[currentIndex];
			currentNode.sphere = node->sphereBuild();

			// Prepare leaf meshlet.
			currentNode.leafMeshletGroupOffset = meshletGroups.size();
			currentNode.leafMeshletGroupCount  = node->leaves.size();
			for (const auto& group : node->leaves)
			{
				GPUGLTFMeshletGroup newGroup{};
				newGroup.clusterPosCenter = group.clusterPosCenter;
				newGroup.error            = group.error;
				newGroup.parentError      = group.parentError;
				newGroup.parentPosCenter  = group.parentPosCenter;
				newGroup.meshletCount     = group.meshletIndices.size();
				newGroup.meshletOffset    = meshletGroupIndices.size();

				meshletGroupIndices.insert(meshletGroupIndices.end(), group.meshletIndices.begin(), group.meshletIndices.end());
				meshletGroups.push_back(std::move(newGroup));
			}

			node->flattenIndex = currentIndex;
		}
		nodeQueue.pop();

		for (auto& child : node->children)
		{
			if (child) { nodeQueue.push(child.get()); }
		}
	}

	std::stack<ClusterParentErrorBVHTree::Node*> countNodeStack;
	countNodeStack.push(&root);

	nodeQueue.push(&root);
	while (!nodeQueue.empty())
	{
		auto* node = nodeQueue.front();
		nodeQueue.pop();

		auto& nodeFlatten = flattenNode.at(node->flattenIndex);
		for (uint i = 0; i < kNaniteBVHLevelNodeCount; i ++)
		{
			auto& child = node->children[i];
			if (child)
			{
				nodeFlatten.children[i] = child->flattenIndex;
				nodeQueue.push(child.get());
				countNodeStack.push(child.get());
			}
			else
			{
				nodeFlatten.children[i] = kUnvalidIdUint32;
			}
		}
	}

	while (!countNodeStack.empty())
	{
		auto* node = countNodeStack.top();
		countNodeStack.pop();

		auto& nodeFlatten = flattenNode.at(node->flattenIndex);
		nodeFlatten.bvhNodeCount = 1;

		for (uint i = 0; i < kNaniteBVHLevelNodeCount; i++)
		{
			auto& child = node->children[i];

			const bool bChild0ValidState = node->children[0] != nullptr;
			const bool bCurrentStateValid = child != nullptr;
			check(bChild0ValidState == bCurrentStateValid);

			if (child)
			{
				auto& nodeFlattenLeft = flattenNode.at(child->flattenIndex);
				nodeFlatten.bvhNodeCount += nodeFlattenLeft.bvhNodeCount;
			}
		}
	}
}

static void buildBVHTree(
	const MeshletContainer& ctx, 
	std::vector<GPUGLTFMeshletGroup>& meshletGroups, 
	std::vector<GPUBVHNode>& flattenNode, 
	std::vector<uint32>& meshletGroupIndices)
{
	std::unordered_map<uint64, uint32> errorSphereGroupMap{ };
	std::vector<ClusterGroup> parentValidMeshlet{ };

	ClusterParentErrorBVHTree bvh { };

	// 0. combine all meshlet find max bounds.
	{
		bvh.root = std::make_unique<ClusterParentErrorBVHTree::Node>();
		bvh.root->depth = 0;

		float3 posMin = float3( FLT_MAX);
		float3 posMax = float3(-FLT_MAX);

		std::unordered_map<uint64, uint32> rootSphereGroupMap{ };

		for (MeshletIndex i = 0; i < ctx.meshlets.size(); i++)
		{
			const auto& meshlet = ctx.meshlets[i];
			const uint64 hashInGroupMap = hashMeshletGroup(meshlet);

			ClusterGroup newGroup{ };
			newGroup.clusterPosCenter = meshlet.clusterPosCenter;
			newGroup.parentError      = meshlet.parentError;
			newGroup.parentPosCenter  = meshlet.parentPosCenter;
			newGroup.error            = meshlet.error;
			newGroup.meshletIndices.push_back(i);

			if (meshlet.isParentSet())
			{
				if (errorSphereGroupMap.contains(hashInGroupMap))
				{
					if (parentValidMeshlet[errorSphereGroupMap[hashInGroupMap]].meshletIndices.size() >= kClusterGroupMergeMaxCount)
					{
						errorSphereGroupMap[hashInGroupMap] = parentValidMeshlet.size();
						parentValidMeshlet.push_back(std::move(newGroup));
					}
					else
					{
						parentValidMeshlet[errorSphereGroupMap[hashInGroupMap]].meshletIndices.push_back(i);
					}
				}
				else
				{
					posMin = math::min(posMin, meshlet.parentPosCenter - meshlet.parentError);
					posMax = math::max(posMax, meshlet.parentPosCenter + meshlet.parentError);

					errorSphereGroupMap[hashInGroupMap] = parentValidMeshlet.size();
					parentValidMeshlet.push_back(std::move(newGroup));
				}
			}
			else
			{
				if (rootSphereGroupMap.contains(hashInGroupMap))
				{
					if (bvh.root->leaves[rootSphereGroupMap[hashInGroupMap]].meshletIndices.size() >= kClusterGroupMergeMaxCount)
					{
						rootSphereGroupMap[hashInGroupMap] = bvh.root->leaves.size();
						bvh.root->leaves.push_back(std::move(newGroup));
					}
					else
					{
						bvh.root->leaves[rootSphereGroupMap[hashInGroupMap]].meshletIndices.push_back(i);
					}

				}
				else
				{
					rootSphereGroupMap[hashInGroupMap] = bvh.root->leaves.size();
					bvh.root->leaves.push_back(std::move(newGroup));
				}
			}
		}

		// Root fill.
		bvh.root->minPos = posMin;
		bvh.root->maxPos = posMax;
	}

	// 1. Recursive build bvh.
	{
		std::vector<const ClusterGroup*> clusterGroupInBounds(parentValidMeshlet.size());
		for (uint i = 0; i < clusterGroupInBounds.size(); i++)
		{
			clusterGroupInBounds[i] = &parentValidMeshlet[i];
		}

		buildBVH(ctx, *bvh.root, std::move(clusterGroupInBounds));
	}
	
	// 2. Flatten bvh.
	flattenBVH(ctx, *bvh.root, meshletGroups, flattenNode, meshletGroupIndices);

	for (const auto& group : meshletGroups)
	{
		check(group.meshletCount <= kClusterGroupMergeMaxCount);
	}
	check(flattenNode[0].bvhNodeCount == flattenNode.size());
}



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

using ConbimeMeshlets = std::vector<uint32>;
static inline std::vector<ConbimeMeshlets> buildOneClusterGroup(const MeshletContainer& ctx)
{
	ConbimeMeshlets result(ctx.meshlets.size());
	for (MeshletIndex i = 0; i < ctx.meshlets.size(); i++)
	{
		result[i] = i;
	}

	return { result };
}

uint32 hashPosition(float3 pos, float posFuseThreshold)
{
#if 0
	return crc::crc32((void*)&pos, sizeof(pos), 0);
#else
	int3 posInt3 = math::ceil(pos / posFuseThreshold);
	return crc::crc32((void*)&posInt3, sizeof(posInt3), 0);
#endif
}

static bool buildClusterGroup(const MeshletContainer& ctx, std::vector<ConbimeMeshlets>& outGroup, const std::vector<Vertex>& vertices, float posFuseThreshold)
{
	const auto& meshlets = ctx.meshlets;

	if (meshlets.size() < kMinNumMeshletPerGroup)
	{
		// No enough meshlet count to group-simplify-split, just return one group.
		outGroup = buildOneClusterGroup(ctx);
		return false;
	}

	const uint32 groupMeshletCount = math::min(uint32(meshlets.size()) / kMinNumMeshletPerGroup, kMaxNumMeshletPerGroup);

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
		idx_t nparts = meshlets.size() / groupMeshletCount;
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

static bool isLooseGeometry(const std::vector<uint32>& indices, const std::vector<Vertex>& vertices)
{
	std::unordered_map<MeshletEdge, uint32, MeshletEdge::Hash> edgeUsedCount;

	const uint triangleCount = indices.size() / 3;
	for (uint triangleId = 0; triangleId < triangleCount; triangleId++) // Loop all triangle.
	{
		for (uint i = 0; i < 3; i++) // Loop all edge.
		{
			uint32 v0 = indices[triangleId * 3 + i];
			uint32 v1 = indices[triangleId * 3 + (i + 1) % 3];

			MeshletEdge edge(v0, v1);
			edgeUsedCount[edge]++;
		}
	}

	uint32 edgeBorderCount = 0;
	for (const auto& pair : edgeUsedCount)
	{
		if (pair.second == 1) // is edge.
		{
			edgeBorderCount++;
		}
	}

	if (edgeBorderCount == edgeUsedCount.size())
	{
		return true;
	}

	return false;
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
	std::vector<ConbimeMeshlets> clusterGroups;
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
					}
				}
			}
		}

		// Simplify group.
		std::vector<VertexIndex> simplifiedVertices(groupMergeVertices.size());
		float simplificationError = 0.f;
		{
			const auto targetIndicesCount = groupMergeVertices.size() * kGroupSimplifyThreshold;
			uint32 options = meshopt_SimplifyLockBorder | meshopt_SimplifySparse | meshopt_SimplifyErrorAbsolute;

			constexpr uint32 kAttributeCount = 9;
			static const float attributeWeights[kAttributeCount] =
			{
				0.05f, 0.05f, // uv
				0.5f, 0.5f, 0.5f, // normal
				0.001f, 0.001f, 0.001f, 0.05f // tangent, .w is sign, weight bigger.
			};

			simplifiedVertices.resize(meshopt_simplifyWithAttributes(
				simplifiedVertices.data(), // destination
				groupMergeVertices.data(), // indices
				groupMergeVertices.size(), // index_count
				&m_vertices[0].position.x, // vertex_positions
				m_vertices.size(),         // vertex_count
				sizeof(m_vertices[0]),     // vertex_positions_stride
				&m_vertices[0].uv0.x,      // vertex_attributes
				sizeof(m_vertices[0]),     // vertex_attributes_stride
				attributeWeights,          // attribute_weights
				kAttributeCount,           // attribute_count
				NULL,                      // vertex_lock
				targetIndicesCount,
				targetError,
				options,
				&simplificationError));
		}

		// Split - half meshlet.
		const auto targetIndicesMin = groupMergeVertices.size() * kGroupSimplifyMinReduce;
		if (simplifiedVertices.size() > 0 && simplifiedVertices.size() < targetIndicesMin)
		{
			float passedError = 0.0f; // Default is 0.0f so if no diff we just skip.
			for (uint32 sMid : clusterGroup)
			{
				// 
				passedError = math::max(passedError, srcCtx.meshlets[sMid].error);
			}
			check(passedError >= 0.0f);

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
		else
		{
			// Current meshlet group can't simplify anymore.
		}
	}
}

MeshletContainer NaniteBuilder::build() const
{
	MeshletContainer finalCtx { };
	checkMsgf(m_indices.size() % 3 == 0, "Nanite only support triangle mesh!");

	// Compute simplify scale for later cluster simplify.
	const float meshOptSimplifyScale = meshopt_simplifyScale(&m_vertices[0].position.x, m_vertices.size(), sizeof(m_vertices[0]));

	// Src mesh input meshlet context, lod0 default use negative error.
	MeshletContainer currentLodCtx = buildMeshlets(m_vertices, m_indices, m_coneWeight, 0, -1.0f, math::vec3(0.0f));

	//
	MeshletContainer nextLodCtx = { };
	for (uint32 lod = 0; lod < (kNaniteMaxLODCount - 1); lod++)
	{
		// Clear next lod ctx.
		nextLodCtx = {};

		const uint32 targetLod = lod + 1;
		const float tagetLodError = float(lod) / float(kNaniteMaxLODCount);

		const float lodErrorAbsolute = math::lerp(kSimplifyErrorMin, kSimplifyErrorMax, tagetLodError) * meshOptSimplifyScale;
		MeshletsGMSS(currentLodCtx, nextLodCtx, lodErrorAbsolute, targetLod);

		// Current lod ctx already update parent data, so merge to final.
		finalCtx.merge(std::move(currentLodCtx));

		if (nextLodCtx.meshlets.empty())
		{
			break;
		}

		// Swap for next lod.
		currentLodCtx = std::move(nextLodCtx);
	}

	buildBVHTree(finalCtx, finalCtx.meshletGroups, finalCtx.bvhNodes, finalCtx.meshletGroupIndices);

	return finalCtx;
}

void fuseVertices(std::vector<uint32>& indices, std::vector<Vertex>& vertices, bool bFuseIgnoreNormal)
{
	std::vector<Vertex> remapVertices;
	remapVertices.reserve(vertices.size());

	std::vector<uint32> remapIndices;
	remapIndices.reserve(indices.size());

	std::map<uint64, size_t> verticesMap;

	// 
	struct HashVertexInfo
	{
		float3 position;
		float2 uv0;
		float2 uv1;
		float4 color0;
		signed char normal[3];
		float tangentW;
	};

	uint fuseCount = 0;

	//
	for (uint32 index : indices)
	{
		const Vertex& vertex = vertices[index];

		HashVertexInfo hashInfo { };
		hashInfo.position = vertex.position;
		hashInfo.uv0 = vertex.uv0;
		hashInfo.uv1 = vertex.uv1;
		hashInfo.color0 = vertex.color0;
		hashInfo.tangentW = vertex.tangent.w;

		if (!bFuseIgnoreNormal)
		{
			hashInfo.normal[0] = (signed char)(meshopt_quantizeSnorm(vertex.normal[0], 8));
			hashInfo.normal[1] = (signed char)(meshopt_quantizeSnorm(vertex.normal[1], 8));
			hashInfo.normal[2] = (signed char)(meshopt_quantizeSnorm(vertex.normal[2], 8));
		}


		const uint64 hashId = cityhash::cityhash64((const char*)&hashInfo, sizeof(HashVertexInfo));
		if (!verticesMap.contains(hashId))
		{
			verticesMap[hashId] = remapVertices.size();
			remapVertices.push_back(vertex);
		}
		else
		{
			fuseCount ++;
		}

		remapIndices.push_back(verticesMap[hashId]);
	}

	LOG_TRACE("Fuse vertices to {1}%.", fuseCount, 100.0f * float(remapVertices.size()) / float(vertices.size()))

	indices  = std::move(remapIndices);
	vertices = std::move(remapVertices);
}

NaniteBuilder::NaniteBuilder(
	std::vector<uint32>&& inputIndices,
	std::vector<Vertex>&& inputVertices,
	bool bFuse,
	bool bFuseIgnoreNormal,
	float coneWeight)
	: m_indices(inputIndices)
	, m_vertices(inputVertices)
	, m_coneWeight(coneWeight)
{
	if (bFuse)
	{
		fuseVertices(m_indices, m_vertices, bFuseIgnoreNormal);
	}

	size_t indexCount = m_indices.size();

	std::vector<uint32> remap(m_indices.size()); // allocate temporary memory for the remap table
	size_t vertexCount = meshopt_generateVertexRemap(&remap[0], m_indices.data(), indexCount, &m_vertices[0], indexCount, sizeof(Vertex));

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
	 vertices.insert(vertices.end(),  rhs.vertices.begin(),  rhs.vertices.end());
	 meshlets.insert(meshlets.end(),  rhs.meshlets.begin(),  rhs.meshlets.end());
}
}