#include "gi.h"

struct GISpecularTracePushConsts
{
    uint2 gbufferDim;
    uint cameraViewId;

    uint roughnessId;
    uint depthId;
    uint normalRSId;
    uint UAV;

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
    bool  bHistoryValid; 

    uint  clipmapConfigBufferId;
    uint  clipmapCount; 
    float skyLightLeaking;
    //////////////////////


};
CHORD_PUSHCONST(GISpecularTracePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.


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

float3 getReflectionDir(float3 view, float3 normal, float roughness, float2 e)
{

    float3x3 tbn = createTBN(normal); 
    float3 Ve = tbnInverseTransform(tbn, -view);

#if 0
    float3 Ne = importanceSampleGGX(e, roughness);
#else
    float3 Ne = importanceSampleGGXVNDF(Ve, roughness, roughness, e.x, e.y);
#endif

    float3 reflectDir_e = reflect(-Ve, Ne);
    return tbnTransform(tbn, reflectDir_e);
}

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;
 
    const uint2 gid = remap8x8(localThreadIndex); 
    const uint2 tid = workGroupId * 8 + gid;

    float2 uv = (tid + 0.5) / pushConsts.gbufferDim;
 
    float4 curTraceRadiance = 0.0;
    if (all(tid < pushConsts.gbufferDim))
    {
        float deviceZ = loadTexture2D_float1(pushConsts.depthId, tid);
        if (deviceZ > 0.0)
        {
            const float3 positionRS = getPositionRS(uv, max(deviceZ, kFloatEpsilon), perView); 
            const float3 viewDirRS = normalize(positionRS);
            
            //
            float roughness = loadTexture2D_float1(pushConsts.roughnessId, tid);
            
            // 
            float3 normalRS = loadTexture2D_float4(pushConsts.normalRSId, tid).xyz * 2.0 - 1.0;
            normalRS = normalize(normalRS);

            const float3 viewDirVS = normalize(mul(perView.translatedWorldToView, float4(viewDirRS, 0.0)).xyz);
            float3 normalVS = normalize(mul(perView.translatedWorldToView, float4(normalRS, 0.0)).xyz);

            //
            float2 blueNoise2 = STBN_float2(scene.blueNoiseCtx, tid, scene.frameCounter);

            //
            float3 reflectDirVS = getReflectionDir(viewDirVS, normalVS, roughness, blueNoise2);
            
            // 
            float3 rayDirection = normalize(mul(perView.viewToTranslatedWorld, float4(reflectDirVS, 0.0)).xyz);
            curTraceRadiance = rayTrace(perView, positionRS, rayDirection, deviceZ);
        }
    }

    storeRWTexture2D_float4(pushConsts.UAV, tid, curTraceRadiance);
}


#endif