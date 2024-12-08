#include "gi.h"

struct GIProbeTracePushConsts
{
    uint2 probeDim;
    uint2 gbufferDim;

    //////////////////////
    uint cascadeCount;
    uint shadowViewId;
    uint shadowDepthIds;
    uint transmittanceId;

    uint scatteringId;
    uint singleMieScatteringId;
    uint linearSampler;
    uint irradianceTextureId;

    float rayMissDistance;
    float maxRayTraceDistance;
    float rayHitLODOffset;
    uint  bHistoryValid;

    uint  clipmapConfigBufferId;
    uint  clipmapCount;
    float skyLightLeaking;
    //////////////////////
 
    uint cameraViewId; 
    uint probeSpawnInfoSRV;

    uint radianceUAV;
    uint historyTraceSRV;
    uint statSRV;
    uint screenProbeSampleSRV;
};
CHORD_PUSHCONST(GIProbeTracePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#define DEBUG_HIT_FACE 0

#include "base.hlsli"  
#include "bindless.hlsli"
#include "gltf.h"
#include "cascade_shadow_projection.hlsli"
#include "colorspace.h" 
#include "debug.hlsli" 
#include "debug_line.hlsli"
#include "raytrace_shared.hlsli"

// Atmosphere shared.
#define ATMOSPHERE_LINEAR_SAMPLER Bindless(SamplerState, pushConsts.linearSampler)
#define DISABLE_ATMOSPHERE_CHECK
#include "atmosphere.hlsli"

[[vk::binding(0, 1)]] RaytracingAccelerationStructure topLevelAS;
#include "gi_raytracing.hlsli"

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const uint2 gid = remap8x8(localThreadIndex); 
    const uint2 tid = workGroupId * 8 + gid; 

    const uint2 probeCoord = workGroupId;
    const uint probeLinearIndex = probeCoord.x + probeCoord.y * pushConsts.probeDim.x;

    GIScreenProbeSpawnInfo spawnInfo;
    { 
        const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.probeSpawnInfoSRV, probeLinearIndex);
        spawnInfo.unpack(packProbeSpawnInfo);
    } 

    // Pre-return if no valid spawn probe.  
    if (!spawnInfo.isValid()) 
    {  
        return; 
    }  
 
    // Ray tracing get radiance.  
    float3 probePositionRS;
    float3 probeNormalRS = spawnInfo.normalRS;  
    if (WaveIsFirstLane()) 
    {
        probePositionRS = spawnInfo.getProbePositionRS(pushConsts.gbufferDim, perView); 
    }
    probePositionRS = WaveReadLaneFirst(probePositionRS); 

    // Current cell ray direction, hemisphere based on probe normal. 
    float3 rayDirection = getScreenProbeCellRayDirection(scene, probeCoord, gid, probeNormalRS);

    // 
    float4 curTraceRadiance = rayTrace(perView, probePositionRS, rayDirection, spawnInfo.depth, 0);
 
    // 
    float4 historyTraceRadiance = loadTexture2D_float4(pushConsts.historyTraceSRV, tid);

    uint currentProbeSample = 0;
    bool bHistoryValid = (historyTraceRadiance.w > kFloatEpsilon);  
    if (bHistoryValid) 
    {
        // Still valid, so load sample. 
        currentProbeSample = loadTexture2D_uint1(pushConsts.screenProbeSampleSRV, probeCoord);
    }

    // 
    float lerpFactor = 1.0 - 1.0 / (1.0 + currentProbeSample);
    curTraceRadiance = lerp(curTraceRadiance, historyTraceRadiance, lerpFactor);

    // 
    float3 statRadiance = 0.0;
    if (WaveIsFirstLane()) 
    {
        if(pushConsts.statSRV != kUnvalidIdUint32 && !perView.bCameraCut)
        {
            statRadiance = loadTexture2D_float3(pushConsts.statSRV, probeCoord);
        }
    }
    statRadiance = WaveReadLaneFirst(statRadiance); 

    if (any(statRadiance > 0.0))
    {
        curTraceRadiance.xyz = giClipAABB(curTraceRadiance.xyz, statRadiance, 0.3);
    }

    // Store radiance result.
    storeRWTexture2D_float4(pushConsts.radianceUAV, tid, curTraceRadiance);
}



#endif // 