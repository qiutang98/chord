#include "base.h"

// Hybrid sdsm and cached cascaded shadow map.

inline bool isCascadeCacheValid(
    bool bCacheValid,
    uint realtimeCount,
    uint cascadeCount,
    uint tickCount,
    uint cascadeId) 
{
    if (!bCacheValid || cascadeId < realtimeCount)
    {
        return false;
    }

    const uint intervalUpdatePeriod = cascadeCount - realtimeCount;
    return (tickCount % intervalUpdatePeriod) != (cascadeId - realtimeCount);
}

struct CascadeSetupPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(CascadeSetupPushConsts);

    float3 lightDir;
    uint cameraViewId;

    float cascadeStartDistance;
    float cascadeEndDistance;
    uint validDepthMinMaxBufferId;
    uint cascadeCount;

    float splitLambda;
    uint cascadeDim;
    uint cascadeViewInfos;
    uint bCacheValid;

    uint realtimeCount;
    uint tickCount;
    float farCascadeSplitLambda;
    float farCsacadeEndDistance;

    float radiusScale;
};
CHORD_PUSHCONST(CascadeSetupPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 
#include "base.hlsli"
#include "debug.hlsli"

float logCascadeSplit(
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

[numthreads(32, 1, 1)]
void cascadeComputeCS(uint localThreadIndex : SV_GroupIndex)
{
    const uint cascadeId = localThreadIndex;
    if (cascadeId >= pushConsts.cascadeCount)
    {
        return;
    }

    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);

    // Always Y up for cascade view setup. 
    const float3 upDir = float3(0.0, 1.0, 0.0);

    const float nearZ = perView.zNear;
    const float farZ  = perView.zFar;

    const float clipRange = farZ - nearZ;


    float minZ;
    float maxZ;
    float splitLambda;
    uint splitCascadeCount;
    uint splitCascadeId;
    float splitStart;

    if (cascadeId < pushConsts.realtimeCount)
    {
        minZ              = nearZ + pushConsts.cascadeStartDistance;
        maxZ              = nearZ + pushConsts.cascadeEndDistance;
        splitLambda       = pushConsts.splitLambda;
        splitCascadeCount = pushConsts.realtimeCount;
        splitCascadeId    = cascadeId;

        if (pushConsts.validDepthMinMaxBufferId != kUnvalidIdUint32)
        {
            ByteAddressBuffer bufferAcess = ByteAddressBindless(pushConsts.validDepthMinMaxBufferId);
            uint minZValidUint = bufferAcess.TypeLoad(uint, 0);
            uint maxZValidUint = bufferAcess.TypeLoad(uint, 1);

            float minZValid = asfloat(minZValidUint);
            float maxZValid = asfloat(maxZValidUint);

            if (maxZValid > 0.0) 
            {
                minZ = max(minZ, nearZ / maxZValid); 
            }

            if (minZValid > 0.0) 
            {
                float stableDistance = pushConsts.cascadeEndDistance - pushConsts.cascadeStartDistance;

                maxZ = max(maxZ, minZ * 1.1);
                maxZ = min(maxZ, minZ + stableDistance);
                maxZ = min(maxZ, nearZ / minZValid);
            }
            check(maxZ > minZ);
        }

        splitStart = minZ - nearZ;
    }
    else
    {
        maxZ = nearZ + pushConsts.farCsacadeEndDistance;
        splitLambda  = pushConsts.farCascadeSplitLambda; // 


        minZ = nearZ + pushConsts.cascadeEndDistance;
        splitStart = pushConsts.cascadeEndDistance;
        
        splitCascadeCount = pushConsts.cascadeCount - pushConsts.realtimeCount;
        splitCascadeId    = cascadeId - pushConsts.realtimeCount;
    }

    float splitDist = logCascadeSplit(nearZ, maxZ, minZ, clipRange, splitCascadeId, splitCascadeCount, splitLambda);

    float prevSplitDist = (splitCascadeId == 0) 
        ? splitStart / clipRange
        : logCascadeSplit(nearZ, maxZ, minZ, clipRange, splitCascadeId - 1, splitCascadeCount, splitLambda);


    float splitDist_0 = logCascadeSplit(nearZ, nearZ, nearZ + pushConsts.farCsacadeEndDistance, clipRange, 0, pushConsts.cascadeCount, pushConsts.farCascadeSplitLambda);
    float prevSplitDist_0 = 0.0;

    // Relative camera world space.
    float3 cascadeFrustumCorner[8];
    {
        cascadeFrustumCorner[0] = float3(-1.0f,  1.0f, 1.0f);
		cascadeFrustumCorner[1] = float3( 1.0f,  1.0f, 1.0f);
		cascadeFrustumCorner[2] = float3( 1.0f, -1.0f, 1.0f);
		cascadeFrustumCorner[3] = float3(-1.0f, -1.0f, 1.0f);

		cascadeFrustumCorner[4] = float3(-1.0f,  1.0f, 0.0f);
		cascadeFrustumCorner[5] = float3( 1.0f,  1.0f, 0.0f);
		cascadeFrustumCorner[6] = float3( 1.0f, -1.0f, 0.0f);
		cascadeFrustumCorner[7] = float3(-1.0f, -1.0f, 0.0f);

		for (uint i = 0; i < 8; i++)
		{
			float4 invCorner = mul(perView.clipToTranslatedWorldWithZFar_NoJitter, float4(cascadeFrustumCorner[i], 1.0f));
			cascadeFrustumCorner[i] = invCorner.xyz / invCorner.w;
		}
    }

    float3 cascadeFrustumCorner_0[8];
    for (uint i = 0; i < 4; i++)
    {
        float3 cornerRay = cascadeFrustumCorner[i + 4] - cascadeFrustumCorner[i]; // distance ray.

        float3 nearCornerRay = cornerRay * prevSplitDist_0;
        float3 farCornerRay = cornerRay * splitDist_0;

        cascadeFrustumCorner_0[i + 4] = cascadeFrustumCorner[i] + farCornerRay;
        cascadeFrustumCorner_0[i + 0] = cascadeFrustumCorner[i] + nearCornerRay;
    }

    // Calculate 4 corner world pos of cascade view frustum.
    for (uint i = 0; i < 4; i++)
    {
        float3 cornerRay = cascadeFrustumCorner[i + 4] - cascadeFrustumCorner[i]; // distance ray.

        float3 nearCornerRay = cornerRay * prevSplitDist;
        float3 farCornerRay = cornerRay * splitDist;

        cascadeFrustumCorner[i + 4] = cascadeFrustumCorner[i] + farCornerRay;
        cascadeFrustumCorner[i + 0] = cascadeFrustumCorner[i] + nearCornerRay;
    }

    // Calculate center pos of view frustum.
    float3 cascadeFrustumCenter = 0.0;
    float3 cascadeFrustumCenter_0 = 0.0;
    for (uint i = 0; i < 8; i++)
    {
        cascadeFrustumCenter += cascadeFrustumCorner[i];
        cascadeFrustumCenter_0 += cascadeFrustumCorner_0[i];
    }
    cascadeFrustumCenter /= 8.0f;
    cascadeFrustumCenter_0 /= 8.0f;

    // Get view sphere bounds radius.
    float sphereRadius = 0.0f;
    float sphereRadius_0 = 0.0f;
    for (uint i = 0; i < 8; ++i)
    {
        float dist = length(cascadeFrustumCorner[i] - cascadeFrustumCenter);
        sphereRadius = max(sphereRadius, dist);

        float dist_0 = length(cascadeFrustumCorner_0[i] - cascadeFrustumCenter_0);
        sphereRadius_0 = max(sphereRadius_0, dist_0);
    }
    // Round 16.
    float cascadeSphereRadius = ceil(sphereRadius * 16.0f) / 16.0f;

    // 
    float maxCascadeSphereRadius = WaveActiveMax(cascadeSphereRadius);

    // 
    float3 maxExtents =  cascadeSphereRadius;
    float3 minExtents = -maxExtents;
    float3 cascadeExtents = maxExtents - minExtents;

    // Z always use max bounds (for PCSS valid z range).
    cascadeExtents.z = maxCascadeSphereRadius * 2.0;


    float radiusScale;
    float zStartBiasScale;
    if (cascadeId >= pushConsts.realtimeCount)
    {
        radiusScale = sphereRadius_0 / sphereRadius;
        zStartBiasScale = 1.0;
    }
    else
    {
        // radiusScale = WaveReadLaneFirst(sphereRadius) / sphereRadius;
        radiusScale = 10.0 * pushConsts.radiusScale / sphereRadius; // Use fix parameter avoid dynamic change.
        radiusScale = radiusScale / (radiusScale + 1.0); // Tone curve avoid large radius when sdsm closer.


        float startDistanceFactor = (minZ - nearZ) / (pushConsts.cascadeEndDistance - pushConsts.cascadeStartDistance);

        // SDSM use min bias avoid leaking so using 0.25
        // SDSM may start with far distance, so add startDistanceFactor avoid acene.
        zStartBiasScale = 0.25 + startDistanceFactor; // + 1.0 - pow(1.0 - df, 2.0);
    }
    radiusScale = min(radiusScale, 1.0);

    // 
    float3 shadowCameraPos = cascadeFrustumCenter - normalize(pushConsts.lightDir) * cascadeExtents.z * 0.5f;

    float nearZProj = 0.0f;
    float farZProj  = cascadeExtents.z;

    float4x4 shadowView = lookAt_RH(shadowCameraPos, cascadeFrustumCenter, upDir);
    float4x4 shadowProj = ortho_RH_ZeroOne(
        minExtents.x,
        maxExtents.x,
        minExtents.y,
        maxExtents.y,
        farZProj, // Also reverse z for shadow depth.
        nearZProj);

    // Texel align.
    const float sMapSize = float(pushConsts.cascadeDim);
    float4x4 shadowViewProjMatrix = mul(shadowProj, shadowView);
    float4 shadowOrigin = float4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowOrigin  = mul(shadowViewProjMatrix, shadowOrigin);
    shadowOrigin *= (sMapSize / 2.0f);

    // Move to center uv pos
    float3 roundedOrigin = round(shadowOrigin.xyz);
    float3 roundOffset = roundedOrigin - shadowOrigin.xyz;
    roundOffset = roundOffset * (2.0f / sMapSize);
    roundOffset.z = 0.0f;

    // Push back round offset data to project matrix.
    shadowProj[0][3] += roundOffset.x;
    shadowProj[1][3] += roundOffset.y;

    // Final proj view matrix
    float4x4 shadowFinalViewProj = mul(shadowProj, shadowView);
    float4x4 reverseToWorld = matrixInverse(shadowFinalViewProj);

    // Build frustum plane.
    float4 planes[6];
    {
        float3 p[8];

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
            float4 invCorner = mul(reverseToWorld, float4(p[i], 1.0f));
            p[i] = invCorner.xyz / invCorner.w; // orth don't need /w
        }

        // left
        float3 leftN = normalize(cross((p[4] - p[7]), (p[3] - p[7])));
        planes[0] = float4(leftN, -dot(leftN, p[7])); // Frustum::eLeft

        // down
        float3 downN = normalize(cross((p[6] - p[2]), (p[3] - p[2])));
        planes[1] = float4(downN, -dot(downN, p[2])); // Frustum::eDown 

        // right
        float3 rightN = normalize(cross((p[6] - p[5]), (p[1] - p[5])));
        planes[2] = float4(rightN, -dot(rightN, p[5])); // Frustum::eRight

        // top
        float3 topN = normalize(cross((p[5] - p[4]), (p[0] - p[4])));
        planes[3] = float4(topN, -dot(topN, p[4])); // Frustum::eTop

        // front
        float3 frontN = normalize(cross((p[1] - p[0]), (p[3] - p[0])));
        planes[4] = float4(frontN, -dot(frontN, p[0])); // Frustum::eFront

        // back
        float3 backN = normalize(cross((p[5] - p[6]), (p[7] - p[6])));
        planes[5] = float4(backN, -dot(frontN, p[6])); // Frustum::eBack
    }
  
    InstanceCullingViewInfo viewInfo;
    viewInfo.translatedWorldToClip   = shadowFinalViewProj;
    viewInfo.clipToTranslatedWorld   = reverseToWorld;
    viewInfo.cameraWorldPos          = perView.cameraWorldPos;
    viewInfo.orthoDepthConvertToView = float4(shadowProj[2][2], shadowProj[2][3], zStartBiasScale, radiusScale);
    viewInfo.renderDimension         = float4(pushConsts.cascadeDim, pushConsts.cascadeDim, 1.0 / pushConsts.cascadeDim, 1.0 / pushConsts.cascadeDim);
    for (uint i = 0; i < 6; i ++)
    {
        viewInfo.frustumPlanesRS[i] = planes[i];
    }

    if (isCascadeCacheValid(pushConsts.bCacheValid, pushConsts.realtimeCount, pushConsts.cascadeCount, pushConsts.tickCount, cascadeId)) 
    {
        // Don't override view info. 
    }
    else
    {
        // Need update view info.
        BATS(InstanceCullingViewInfo, pushConsts.cascadeViewInfos, cascadeId, viewInfo);
    }
}


#endif 