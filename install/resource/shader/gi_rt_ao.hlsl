#include "gi.h"

struct GIRTAOPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(GIRTAOPushConsts);

    uint2 gbufferDim;
    uint cameraViewId;

    uint depthSRV;
    uint normalRSId;

    float rayLength;
    float power;
    uint rtAO_UAV;
};
CHORD_PUSHCONST(GIRTAOPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "base.hlsli"  
#include "bindless.hlsli"
#include "gltf.h"
#include "cascade_shadow_projection.hlsli"
#include "colorspace.h" 
#include "debug.hlsli" 
#include "debug_line.hlsli"
#include "raytrace_shared.hlsli"

[[vk::binding(0, 1)]] RaytracingAccelerationStructure topLevelAS;

//
[numthreads(8, 4, 1)]
void mainCS(uint2 tid : SV_DispatchThreadID)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    if (any(tid >= pushConsts.gbufferDim))
    {
        return;
    }

    const float2 texelSize = 1.0 / pushConsts.gbufferDim;
    const float2 uv = (tid + 0.5) * texelSize; 

    float AO = 1.0f;
    const float deviceZ = loadTexture2D_float1(pushConsts.depthSRV, tid);
    if (deviceZ > 0.0)
    {
        const float3 positionRS = getPositionRS(uv, deviceZ, perView); 

        float3 normalRS = loadTexture2D_float4(pushConsts.normalRSId, tid).xyz * 2.0 - 1.0;
        normalRS = normalize(normalRS);

        float3 dir_t = uniformSampleHemisphere(STBN_float2(scene.blueNoiseCtx, tid, scene.frameCounter)).xyz;
        float3x3 tbn = createTBN(normalRS); 
        float3 rayDir = tbnTransform(tbn, dir_t);

        float rayStart =  getRayTraceStartOffset(perView.zNear, deviceZ);

        GIRayQuery query;
        RayDesc ray = getRayDesc(positionRS, rayDir, rayStart, pushConsts.rayLength);
        const uint traceFlag = RAY_FLAG_CULL_NON_OPAQUE;

        query.TraceRayInline(topLevelAS, traceFlag, 0xFF, ray);
        query.Proceed(); 

        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            float t = query.CommittedRayT();
            float normalize_t = saturate(t / pushConsts.rayLength);
            AO = pow(normalize_t, pushConsts.power);
        }
    }
    storeRWTexture2D_float1(pushConsts.rtAO_UAV, tid, AO);
}

#endif 