#include "gi.h"

struct GISpecularSpatialFilterPushConsts
{
    uint2 gbufferDim;
    int2 direction;

    uint cameraViewId; 
    uint depthSRV;
    uint originSRV;
    uint normalRS;
    uint roughnessSRV;

    uint SRV;
    uint UAV;
    uint statSRV;
};
CHORD_PUSHCONST(GISpecularSpatialFilterPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const int2 gid = remap8x8(localThreadIndex); 
    const int2 tid = workGroupId * 8 + gid;

    if (any(tid >= pushConsts.gbufferDim))
    {
        // Out of bound pre-return.  
        return; 
    }

    float deviceZ = loadTexture2D_float1(pushConsts.depthSRV, tid);
    if (deviceZ <= 0.0)
    {
        storeRWTexture2D_float4(pushConsts.UAV, tid, 0.0);
        return;
    }

    const float2 texelSize = 1.0 / pushConsts.gbufferDim;
    float4 centerSpecular =  loadTexture2D_float4(pushConsts.SRV, tid);

    // 
    float3 centerNormalRS = loadTexture2D_float4(pushConsts.normalRS, tid).xyz * 2.0 - 1.0;
    centerNormalRS = normalize(centerNormalRS);

    float2 uv = (tid + 0.5) * texelSize;
    float3 centerPositionRS =  getPositionRS(uv, max(deviceZ, kFloatEpsilon), perView); 

    float distanceScaleFactor = max(1e-3f, length(centerPositionRS));

    float roughness = loadTexture2D_float1(pushConsts.roughnessSRV, tid);
    if (roughness == 0.0)
    {
        storeRWTexture2D_float4(pushConsts.UAV, tid, centerSpecular);
        return; // Skip mirror surface. 
    }

    float w = loadTexture2D_float2(pushConsts.originSRV, tid).y;

    float4 specularAccumulate = 0.0;
    float specularWeightSum   = 0.0;

    int filteredRadius;
    if (roughness >= kGIMaxGlossyRoughness)
    {
        // Will fallback to diffuse, so use diffuse's filter radius. 
        filteredRadius = loadTexture2D_float2(pushConsts.originSRV, tid).x * 8.0;
    }
    else
    {
        float lerpFactor = saturate(roughness * (1.0 / kGIMaxGlossyRoughness)); // Remap clamp roughness to 0 - 1.
        filteredRadius = lerp(8.0, 16.0, lerpFactor);

        filteredRadius = lerp(filteredRadius * 0.25, filteredRadius, sqrt(w));
    }

    if (filteredRadius == 0)
    {
        storeRWTexture2D_float4(pushConsts.UAV, tid, centerSpecular);
        return; // Skip zero filter radius surface. 
    }

    for (int i = -filteredRadius; i <= filteredRadius; i ++)
    {
        int2 sampleCoord = tid + pushConsts.direction * i;

        if (any(sampleCoord < 0) || any(sampleCoord >= pushConsts.gbufferDim))
        {
            continue;
        }

        float sampleDeviceZ = loadTexture2D_float1(pushConsts.depthSRV, sampleCoord);
        if (sampleDeviceZ <= 0.0)
        {
            continue;
        }

        float4 sampleSpecular = loadTexture2D_float4(pushConsts.SRV, sampleCoord);

        float2 sampleUv = (sampleCoord + 0.5) * texelSize;
        float3 samplePositionRS =  getPositionRS(sampleUv, max(sampleDeviceZ, kFloatEpsilon), perView); 

        float3 sampleNormalRS = loadTexture2D_float4(pushConsts.normalRS, sampleCoord).xyz * 2.0 - 1.0;
        sampleNormalRS = normalize(sampleNormalRS);

        float normalFactor = pow(saturate(dot(centerNormalRS, sampleNormalRS)), 16.0);
        float distanceFactor = saturate(1.0 - length(samplePositionRS - centerPositionRS) / distanceScaleFactor);

        float weight = pow(normalFactor * distanceFactor, 16.0);
        if (sampleSpecular.w > 1e-6f)
        {
            // weight *= 1.0 / (1.0 + x * x + y * y);

            specularWeightSum  += weight;
            specularAccumulate += sampleSpecular * weight;
        }
    }

    if (specularWeightSum <= 1e-6f)
    {
        storeRWTexture2D_float4(pushConsts.UAV, tid, centerSpecular);
        return; // Skip zero filter radius surface. 
    }

    specularAccumulate /= max(1e-6f, specularWeightSum);
    if (any(isnan(specularAccumulate))) 
    {
        specularAccumulate = 0.0;
    }

    float3 statRadiance = 0.0;
    if (WaveIsFirstLane()) 
    {
        if (pushConsts.statSRV != kUnvalidIdUint32 && all(workGroupId < (pushConsts.gbufferDim / 8)))
        {   
            statRadiance = loadTexture2D_float3(pushConsts.statSRV, workGroupId);
        }
    }
    statRadiance = WaveReadLaneFirst(statRadiance); 

    if (any(statRadiance > 0.0) && roughness > 0.0)
    {
        float weight = 1.0 / (1.0 + specularAccumulate.w);
        float clipRange = lerp(0.25, 0.1, weight);

        clipRange = lerp(clipRange, clipRange * 0.5, sqrt(w));
        specularAccumulate.xyz = giClipAABB(specularAccumulate.xyz, statRadiance, clipRange);
    }

    storeRWTexture2D_float4(pushConsts.UAV, tid, specularAccumulate);
}
#endif 