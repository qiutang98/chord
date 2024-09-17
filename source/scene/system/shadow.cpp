#include <scene/scene.h>
#include <scene/system/shadow.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <ui/ui_helper.h>

using namespace chord;
using namespace chord::graphics;

static float sVSMBasicMipLevelOffset = -0.5f;
static AutoCVarRef cVarVSMBasicMipLevelOffset(
	"r.vsm.basicMipLevelOffset",
	sVSMBasicMipLevelOffset,
	"Virtual shadow map basic mip level offset."
);

static inline float logCascadeSplit(
	const float nearZ,
	const float farDepthPlane,
	const float nearDepthPlane,
	const float clipRange,
	const uint  cascadeId,
	const uint  cascadeCount,
	const float cascadeSplitLambda)
{
	float range = farDepthPlane - nearDepthPlane;
	float ratio = farDepthPlane / nearDepthPlane;

	// get current part factor.
	float p = float(cascadeId + 1) / float(cascadeCount);

	// get log scale factor and uniform scale factor.
	float logScale = nearDepthPlane * pow(abs(ratio), p);
	float uniformScale = nearDepthPlane + range * p;

	// final get split distance.
	float d = cascadeSplitLambda * (logScale - uniformScale) + uniformScale;
	return (d - nearZ) / clipRange;
}

static inline void cascadeSetup(
	ICamera& camera, 
	const CascadeShadowMapConfig& config,
	const float4x4& invertViewProj,
	const float3& lightDirection,
	float splitLambda)
{
	const float nearZ = camera.getZNear();
	const float farZ  = camera.getZFar();

	float3 frustumCorner[8];
	{
		frustumCorner[0] = float3(-1.0f,  1.0f, 1.0f);
		frustumCorner[1] = float3( 1.0f,  1.0f, 1.0f);
		frustumCorner[2] = float3( 1.0f, -1.0f, 1.0f);
		frustumCorner[3] = float3(-1.0f, -1.0f, 1.0f);

		frustumCorner[4] = float3(-1.0f,  1.0f, 0.0f);
		frustumCorner[5] = float3( 1.0f,  1.0f, 0.0f);
		frustumCorner[6] = float3( 1.0f, -1.0f, 0.0f);
		frustumCorner[7] = float3(-1.0f, -1.0f, 0.0f);

		for (uint i = 0; i < 8; i++)
		{
			float4 invCorner = invertViewProj * float4(frustumCorner[i], 1.0f);
			frustumCorner[i].x = invCorner.x / invCorner.w;
			frustumCorner[i].y = invCorner.y / invCorner.w;
			frustumCorner[i].z = invCorner.z / invCorner.w;
		}
	}

	const float clipRange = farZ - nearZ;
	const float minZ = nearZ + config.cascadeStartDistance;
	const float maxZ = nearZ + config.cascadeEndDistance;
	check(maxZ > minZ);

	std::vector<float> cascadeSplits(config.cascadeCount);

	// Always y up.
	const float3 upDir = float3(0.0, 1.0, 0.0);
	const float3 lightDir = normalize(lightDirection);

	// Get split factor.
	for (uint cascadeId = 0; cascadeId < config.cascadeCount; cascadeId++)
	{
		cascadeSplits[cascadeId] = logCascadeSplit(nearZ, maxZ, minZ, clipRange, cascadeId, config.cascadeCount, splitLambda);
	}

	// 
	for (uint cascadeId = 0; cascadeId < config.cascadeCount; cascadeId++)
	{
		float splitDist = cascadeSplits[cascadeId];
		float prevSplitDist = cascadeId == 0 ? (config.cascadeStartDistance) / clipRange : cascadeSplits[cascadeId - 1];

		float3 cascadeFrustumCorner[8];
		for (uint i = 0; i < 8; i++)
		{
			cascadeFrustumCorner[i] = frustumCorner[i];
		}

		// Calculate 4 corner world pos of cascade view frustum.
		for (uint i = 0; i < 4; i++)
		{
			float3 cornerRay = cascadeFrustumCorner[i + 4] - cascadeFrustumCorner[i]; // distance ray.

			float3 nearCornerRay = cornerRay * prevSplitDist;
			float3 farCornerRay  = cornerRay * splitDist;

			cascadeFrustumCorner[i + 4] = cascadeFrustumCorner[i] + farCornerRay;
			cascadeFrustumCorner[i + 0] = cascadeFrustumCorner[i] + nearCornerRay;
		}

		// Calculate center pos of view frustum.
		float3 cascadeFrustumCenter = float3(0.0f);
		for (uint i = 0; i < 8; i++)
		{
			cascadeFrustumCenter += cascadeFrustumCorner[i];
		}
		cascadeFrustumCenter /= 8.0f;

		// Get view sphere bounds radius.
		float sphereRadius = 0.0f;
		for (uint i = 0; i < 8; ++i)
		{
			float dist = length(cascadeFrustumCorner[i] - cascadeFrustumCenter);
			sphereRadius = math::max(sphereRadius, dist);
		}
		// Round 16.
		sphereRadius = ceil(sphereRadius * 16.0f) / 16.0f;

		float3 maxExtents = float3(sphereRadius);
		float3 minExtents = -maxExtents;
		float3 cascadeExtents = maxExtents - minExtents;

		// create temporary view project matrix for cascade.
		float3 shadowCameraPos = cascadeFrustumCenter - normalize(lightDir) * cascadeExtents.z * 0.5f;

		float nearZProj = 0.0f;
		float farZProj  = cascadeExtents.z;

		float4x4 shadowView = math::lookAtRH(shadowCameraPos, cascadeFrustumCenter, upDir);
		float4x4 shadowProj = math::orthoRH_ZO(
			minExtents.x,
			maxExtents.x,
			minExtents.y,
			maxExtents.y,
			farZProj, // Also reverse z for shadow depth.
			nearZProj
		);

		// Texel align.
		const float sMapSize = float(config.cascadeDim);
		float4x4 shadowViewProjMatrix = shadowProj * shadowView;
		float4 shadowOrigin = float4(0.0f, 0.0f, 0.0f, 1.0f);
		shadowOrigin = shadowViewProjMatrix * shadowOrigin;
		shadowOrigin *= (sMapSize / 2.0f);

		// Move to center uv pos
		float3 roundedOrigin = math::round(float3(shadowOrigin));
		float3 roundOffset = roundedOrigin - float3(shadowOrigin);
		roundOffset = roundOffset * (2.0f / sMapSize);
		roundOffset.z = 0.0f;

		// Push back round offset data to project matrix.
		shadowProj[3][0] += roundOffset.x;
		shadowProj[3][1] += roundOffset.y;

		// Final proj view matrix
		float4x4 shadowFinalViewProj = shadowProj * shadowView;
		float4x4 reverseToWorld = inverse(shadowFinalViewProj);

		// Build frustum plane.
		float3 p[8];
		float4 planes[6];
		{
			p[0] = float3(-1.0f,  1.0f, 1.0f);
			p[1] = float3( 1.0f,  1.0f, 1.0f);
			p[2] = float3( 1.0f, -1.0f, 1.0f);
			p[3] = float3(-1.0f, -1.0f, 1.0f);

			p[4] = float3(-1.0f,  1.0f, 0.0f);
			p[5] = float3( 1.0f,  1.0f, 0.0f);
			p[6] = float3( 1.0f, -1.0f, 0.0f);
			p[7] = float3(-1.0f, -1.0f, 0.0f);

			for (uint i = 0; i < 8; i++)
			{
				float4 invCorner = reverseToWorld * float4(p[i], 1.0f);
				p[i] = float3(invCorner) / invCorner.w;
			}

			// left
			float3 leftN = normalize(cross((p[4] - p[7]), (p[3] - p[7])));
			planes[Frustum::eLeft] = float4(leftN, -dot(leftN, p[7]));

			// down
			float3 downN = normalize(cross((p[6] - p[2]), (p[3] - p[2])));
			planes[Frustum::eDown] = float4(downN, -dot(downN, p[2]));

			// right
			float3 rightN = normalize(cross((p[6] - p[5]), (p[1] - p[5])));
			planes[Frustum::eRight] = float4(rightN, -dot(rightN, p[5]));

			// top
			float3 topN = normalize(cross((p[5] - p[4]), (p[0] - p[4])));
			planes[Frustum::eTop] = float4(topN, -dot(topN, p[4]));

			// front
			float3 frontN = normalize(cross((p[1] - p[0]), (p[3] - p[0])));
			planes[Frustum::eFront] = float4(frontN, -dot(frontN, p[0]));

			// back
			float3 backN = normalize(cross((p[5] - p[6]), (p[7] - p[6])));
			planes[Frustum::eBack] = float4(backN, -dot(frontN, p[6]));
		}
	}
}

ShadowManager::ShadowManager()
	: ISceneSystem("Shadow", ICON_FA_SUN + std::string("  Shadow"))
{

}

void ShadowManager::update(const ApplicationTickData& tickData)
{


}

void ShadowManager::onDrawUI(SceneRef scene)
{

}