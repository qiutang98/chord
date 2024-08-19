#include <asset/nanite_builder.h>

#include <metis/metis.h>
#include <shader/base.h>
#include <utils/log.h>
#include <utils/cityhash.h>

using namespace chord;
using namespace chord::nanite;

// Group-merge-simplify-split parameters.
constexpr uint32 kMinNumMeshletPerGroup = 2;
constexpr uint32 kMaxNumMeshletPerGroup = 8;
constexpr uint32 kNumGroupSplitAfterSimplify = 2;
constexpr float  kGroupSimplifyThreshold = 1.0f / float(kNumGroupSplitAfterSimplify);

// At least next level lod need to reduce 20% triangles, otherwise it is no meaning to store it.
constexpr auto kGroupSimplifyMinReduce = 0.8f; 

// Simplify error relative to extent.
constexpr auto kSimplifyError = 0.01f;

// Group merge position error, relative to simplify error. 
constexpr auto kGroupMergePosError = 0.1f;    

using MeshletIndex = uint32;
using VertexIndex = uint32;
using ClusterGroup = std::vector<MeshletIndex>;

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
			return float4(
				0.5f * (maxPos + minPos), 
				0.5f * math::length(maxPos - minPos));
		}

		std::unique_ptr<Node> left = nullptr;
		std::unique_ptr<Node> right = nullptr;

		std::vector<MeshletIndex> leaves;

		uint flattenIndex = kUnvalidIdUint32;
	};

	std::unique_ptr<Node> root;
};

static uint64 hashErrorSphere(float4 sphere)
{
	return cityhash::cityhash64((const char*)&sphere, sizeof(sphere));
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
		auto* iterNodePtr = nodeQueue.front().node;
		std::vector<const ClusterGroup*> clusterGroupInBounds = std::move(nodeQueue.front().clusterGroupInBounds);

		// Pop current node.
		nodeQueue.pop();

		if (clusterGroupInBounds.empty())
		{
			continue;
		}

		// Build leaf.
		if (clusterGroupInBounds.size() == 1)
		{
			for (const auto* groupPtr : clusterGroupInBounds)
			{
				const auto& group = *groupPtr;
				for (const auto& meshletIndex : group)
				{
					iterNodePtr->leaves.push_back(meshletIndex);
				}
			}
			continue;
		}

		// Find longest axis.
		uint longestAxis = 0;
		{
			float3 minPos = iterNodePtr->minPos;
			float3 maxPos = iterNodePtr->maxPos;
			float3 max2min = maxPos - minPos;

			// Use longest axis to split space.
			if (max2min.y >= max2min.x && max2min.y >= max2min.z) { longestAxis = 1; } // y axis
			if (max2min.z >= max2min.x && max2min.z >= max2min.y) { longestAxis = 2; } // z axis
		}

		// Sort group on longest axis.
		std::vector<std::pair<const ClusterGroup*, float>> meshletCenterInLongestAxisSort;
		for (const auto* groupPtr : clusterGroupInBounds)
		{
			meshletCenterInLongestAxisSort.push_back({ groupPtr, ctx.meshlets[groupPtr->at(0)].parentPosCenter[longestAxis] });
		}
		std::ranges::sort(meshletCenterInLongestAxisSort, [](const auto& a, const auto& b) { return a.second < b.second; });


		// Slit node.
		check(meshletCenterInLongestAxisSort.size() >= 2);
		{
			iterNodePtr->left = std::make_unique<ClusterParentErrorBVHTree::Node>();

			float3 posMin = float3( FLT_MAX);
			float3 posMax = float3(-FLT_MAX);

			std::vector<const ClusterGroup*> leftGroups{ };
			for (uint i = 0; i < meshletCenterInLongestAxisSort.size() / 2; i++)
			{
				const auto& group = *(meshletCenterInLongestAxisSort[i].first);

				for (const uint meshletIndex : group)
				{
					const auto& meshlet = ctx.meshlets[meshletIndex];

					posMin = math::min(posMin, meshlet.parentPosCenter - meshlet.parentError);
					posMax = math::max(posMax, meshlet.parentPosCenter + meshlet.parentError);
				}


				leftGroups.push_back(&group);
			}

			iterNodePtr->left->maxPos = posMax;
			iterNodePtr->left->minPos = posMin;

			nodeQueue.push({ .clusterGroupInBounds = std::move(leftGroups), .node = iterNodePtr->left.get()});
		}
		{
			iterNodePtr->right = std::make_unique<ClusterParentErrorBVHTree::Node>();

			float3 posMin = float3( FLT_MAX);
			float3 posMax = float3(-FLT_MAX);

			std::vector<const ClusterGroup*> rightGroups{ };
			for (uint i = meshletCenterInLongestAxisSort.size() / 2; i < meshletCenterInLongestAxisSort.size(); i++)
			{
				const auto& group = *(meshletCenterInLongestAxisSort[i].first);

				for (const uint meshletIndex : group)
				{
					const auto& meshlet = ctx.meshlets[meshletIndex];

					posMin = math::min(posMin, meshlet.parentPosCenter - meshlet.parentError);
					posMax = math::max(posMax, meshlet.parentPosCenter + meshlet.parentError);
				}

				rightGroups.push_back(&group);
			}

			iterNodePtr->right->maxPos = posMax;
			iterNodePtr->right->minPos = posMin;

			nodeQueue.push({ .clusterGroupInBounds = std::move(rightGroups), .node = iterNodePtr->right.get() });
		}
	}
}

static void flattenBVH(const MeshletContainer& ctx, ClusterParentErrorBVHTree::Node& root, 
	std::vector<GPUBVHNode>& flattenNode, 
	std::vector<MeshletIndex>& leafMeshletIndices)
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

			uint leafIterCount = 0;
			float4 loopMeshletSphere = float4(-1.0f);

			// Prepare leaf meshlet.
			currentNode.leafMeshletOffset = leafMeshletIndices.size();
			currentNode.leafMeshletCount = node->leaves.size();
			for (uint i = 0; i < node->leaves.size(); i++)
			{
				uint32 meshletIndex = node->leaves[i];

				const auto& meshlet = ctx.meshlets.at(meshletIndex);
				float4 meshletSphere = float4(meshlet.parentPosCenter, meshlet.parentError);
				if (i == 0)
				{
					loopMeshletSphere = meshletSphere;
				}
				else if(node != &root)
				{
					float diff = math::length2(loopMeshletSphere - meshletSphere);
					check(diff < FLT_EPSILON);
				}

				leafMeshletIndices.push_back(meshletIndex);
			}

			if (node->leaves.size() > 0 && node != &root)
			{
				float diff = math::length2(loopMeshletSphere - currentNode.sphere);
				if (diff >= FLT_EPSILON)
				{
					currentNode.sphere = loopMeshletSphere;
				}
			}

			node->flattenIndex = currentIndex;
		}
		nodeQueue.pop();
		if (node->left) { nodeQueue.push(node->left.get()); }
		if (node->right) { nodeQueue.push(node->right.get()); }
	}

	std::stack<ClusterParentErrorBVHTree::Node*> countNodeStack;
	countNodeStack.push(&root);

	nodeQueue.push(&root);
	while (!nodeQueue.empty())
	{
		auto* node = nodeQueue.front();
		nodeQueue.pop();

		auto& nodeFlatten = flattenNode.at(node->flattenIndex);

		if (node->left) 
		{ 
			nodeFlatten.left = node->left->flattenIndex;
			nodeQueue.push(node->left.get()); 
			countNodeStack.push(node->left.get());
		}
		else
		{
			nodeFlatten.left = kUnvalidIdUint32;
		}
		if (node->right)
		{ 
			nodeFlatten.right = node->right->flattenIndex;
			nodeQueue.push(node->right.get()); 
			countNodeStack.push(node->right.get());
		}
		else
		{
			nodeFlatten.right = kUnvalidIdUint32;
		}
	}

	while (!countNodeStack.empty())
	{
		auto* node = countNodeStack.top();
		countNodeStack.pop();

		auto& nodeFlatten = flattenNode.at(node->flattenIndex);
		nodeFlatten.bvhNodeCount = 1;

		if (node->left)
		{
			auto& nodeFlattenLeft = flattenNode.at(node->left->flattenIndex);
			nodeFlatten.bvhNodeCount += nodeFlattenLeft.bvhNodeCount;
		}

		if (node->right)
		{
			auto& nodeFlattenRight = flattenNode.at(node->right->flattenIndex);
			nodeFlatten.bvhNodeCount += nodeFlattenRight.bvhNodeCount;
		}
	}
}

static void buildBVHTree(const MeshletContainer& ctx, std::vector<GPUBVHNode>& flattenNode, std::vector<MeshletIndex>& leafMeshletIndices)
{
	std::unordered_map<uint64, uint32> errorSphereGroupMap{ };
	std::vector<ClusterGroup> parentValidMeshlet{ };

	ClusterParentErrorBVHTree bvh { };

	// 0. combine all meshlet find max bounds.
	{
		bvh.root = std::make_unique<ClusterParentErrorBVHTree::Node>();
		
		float3 posMin = float3( FLT_MAX);
		float3 posMax = float3(-FLT_MAX);

		for (MeshletIndex i = 0; i < ctx.meshlets.size(); i++)
		{
			const auto& meshlet = ctx.meshlets[i];
			if (meshlet.isParentSet())
			{
				const uint64 hashInGroupMap = hashErrorSphere(float4(meshlet.parentPosCenter, meshlet.parentError));
				if (errorSphereGroupMap.contains(hashInGroupMap))
				{
					parentValidMeshlet[errorSphereGroupMap[hashInGroupMap]].push_back(i);
				}
				else
				{
					posMin = math::min(posMin, meshlet.parentPosCenter - meshlet.parentError);
					posMax = math::max(posMax, meshlet.parentPosCenter + meshlet.parentError);

					errorSphereGroupMap[hashInGroupMap] = parentValidMeshlet.size();
					parentValidMeshlet.push_back({ i });
				}
			}
			else
			{
				// Fill root leaf if parent no set.
				bvh.root->leaves.push_back(i);
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
	flattenBVH(ctx, *bvh.root, flattenNode, leafMeshletIndices);
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

static bool buildClusterGroup(const MeshletContainer& ctx, std::vector<ClusterGroup>& outGroup, const std::vector<Vertex>& vertices, float posFuseThreshold)
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

			simplifiedVertices.resize(meshopt_simplify(
				simplifiedVertices.data(),
				groupMergeVertices.data(),
				groupMergeVertices.size(),
				&m_vertices[0].position.x,
				m_vertices.size(),
				sizeof(m_vertices[0]),
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

	buildBVHTree(finalCtx, finalCtx.bvhNodes, finalCtx.bvhLeafMeshletIndices);

	return finalCtx;
}

struct FuseVertex
{
	int3 hashPos;
	int2 hashUv;
	Vertex cacheVertex;
};

void fuseVertices(std::vector<uint32>& indices, std::vector<Vertex>& vertices, float fuseDistance)
{
	if (fuseDistance < 0.0f) { return; }


}

NaniteBuilder::NaniteBuilder(
	std::vector<uint32>&& inputIndices,
	std::vector<Vertex>&& inputVertices,
	float fuseDistance,
	float coneWeight)
	: m_indices(inputIndices)
	, m_vertices(inputVertices)
	, m_coneWeight(coneWeight)
{
	const bool bInputIsLooseGeometry = isLooseGeometry(m_indices, m_vertices);
	if (bInputIsLooseGeometry)
	{
		LOG_TRACE("Nanite builder find loose geometry input, fusing vertex...");

		// Fuse vertices first.
		fuseVertices(m_indices, m_vertices, fuseDistance);
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
	 vertices.insert(vertices.end(),  rhs.vertices.begin(),  rhs.vertices.end());
	 meshlets.insert(meshlets.end(),  rhs.meshlets.begin(),  rhs.meshlets.end());
}
