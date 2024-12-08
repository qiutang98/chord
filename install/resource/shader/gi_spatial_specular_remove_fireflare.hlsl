#include "gi.h"

struct GISpecularRemoveFireFlareFilterPushConsts
{
    uint2 gbufferDim;

    uint cameraViewId; 
    uint depthSRV;

    uint normalRS;
    uint roughnessSRV;

    uint SRV;
    uint UAV;
    uint statSRV;
};
CHORD_PUSHCONST(GISpecularRemoveFireFlareFilterPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

static const int kSampleCount = 16;
static const int2 kSampleOffsets[kSampleCount] = 
{
    int2( 0,  0), 
    int2( 0,  1),  
    int2(-2,  1),  
    int2( 2, -3), 
    int2(-3,  0),  
    int2( 1,  2), 
    int2(-1, -2), 
    int2( 3,  0), 
    int2(-3,  3),
    int2( 0, -3), 
    int2(-1, -1), 
    int2( 2,  1),  
    int2(-2, -2), 
    int2( 1,  0), 
    int2( 0,  2),   
    int2( 3, -1),
};

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

    float4 specularAccumulate = 0.0;
    float specularWeightSum   = 0.0;

    for (int i = 0; i < 16; i ++)
    {
        int2 sampleCoord = tid + kSampleOffsets[i];

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
        // if (sampleSpecular.w > 1e-6f)
        {
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
        float lerpFactor = saturate(roughness * (1.0 / kGIMaxGlossyRoughness));
        float clipRange = lerp(0.3, 0.5, lerpFactor);
        specularAccumulate.xyz = giClipAABB(specularAccumulate.xyz, statRadiance, clipRange);
    }

    storeRWTexture2D_float4(pushConsts.UAV, tid, specularAccumulate);
}
#endif 