#include <asset/compute_tangent.h>
#include <asset/mikktspace.h>

bool chord::computeTangent(
	std::vector<nanite::Vertex>& rawVertices, 
	std::vector<uint32>& rawIndices)
{
	// MikkTSpace context.
	struct MikkTSpaceContext
	{
		std::vector<nanite::Vertex>* rawVerticesPtr;
		std::vector<uint32>* rawIndicesPtr;
	} 
	computeCtx
	{
		.rawVerticesPtr = &rawVertices,
		.rawIndicesPtr = &rawIndices,
	};

	SMikkTSpaceContext ctx{};
	SMikkTSpaceInterface ctxI{};

	ctx.m_pInterface = &ctxI;
	ctx.m_pUserData  = &computeCtx;

	ctx.m_pInterface->m_getNumFaces = [](const SMikkTSpaceContext* pContext) -> int { return ((MikkTSpaceContext*)pContext->m_pUserData)->rawIndicesPtr->size() / 3; };
	ctx.m_pInterface->m_getNumVerticesOfFace = [](const SMikkTSpaceContext* pContext, const int iFace) -> int { return 3; };
	ctx.m_pInterface->m_getPosition = [](const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert) -> void
	{
		auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
		uint32 id = ctx->rawIndicesPtr->at(iFace * 3 + iVert);

		fvPosOut[0] = ctx->rawVerticesPtr->at(id).position.x;
		fvPosOut[1] = ctx->rawVerticesPtr->at(id).position.y;
		fvPosOut[2] = ctx->rawVerticesPtr->at(id).position.z;
	};
	ctx.m_pInterface->m_getNormal = [](const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert) -> void
	{
		auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
		uint32 id = ctx->rawIndicesPtr->at(iFace * 3 + iVert);

		fvNormOut[0] = ctx->rawVerticesPtr->at(id).normal.x;
		fvNormOut[1] = ctx->rawVerticesPtr->at(id).normal.x;
		fvNormOut[2] = ctx->rawVerticesPtr->at(id).normal.x;
	};
	ctx.m_pInterface->m_getTexCoord = [](const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert) -> void
	{
		auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
		uint32 id = ctx->rawIndicesPtr->at(iFace * 3 + iVert);

		fvTexcOut[0] = ctx->rawVerticesPtr->at(id).uv0.x;
		fvTexcOut[1] = ctx->rawVerticesPtr->at(id).uv0.y;
	};
	ctx.m_pInterface->m_setTSpaceBasic = [](const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
	{
		auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
		uint32 id = ctx->rawIndicesPtr->at(iFace * 3 + iVert);

		ctx->rawVerticesPtr->at(id).tangent.x = fvTangent[0];
		ctx->rawVerticesPtr->at(id).tangent.y = fvTangent[1];
		ctx->rawVerticesPtr->at(id).tangent.z = fvTangent[2];
		ctx->rawVerticesPtr->at(id).tangent.w = fSign;
	};

	return genTangSpaceDefault(&ctx);
}