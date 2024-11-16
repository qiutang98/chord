
#include "ddgi.h"

struct DDGIConvolutionPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(DDGIConvolutionPushConsts);

    uint cameraViewId;
    uint ddgiConfigBufferId;
    uint ddgiConfigId;
    uint UAV; 
 
    uint probeTraceLinearIndexSRV;
    uint probeTracedMarkSRV;
    uint probeCacheRayGbufferSRV;
    uint probeRadianceSRV;

    uint cmdBufferId;
    uint probeCounterSRV;
    uint probeCacheInfoSRV;
    uint probeHistoryValidUAV;
};
CHORD_PUSHCONST(DDGIConvolutionPushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli"
#include "bindless.hlsli"
#include "gltf.h"
#include "cascade_shadow_projection.hlsli"
#include "colorspace.h"

[numthreads(1, 1, 1)]
void indirectCmdParamCS()
{
    uint4 cmd;
    cmd.x = BATL(uint, pushConsts.probeCounterSRV, 1); // Trace. 
    cmd.y = 1;
    cmd.z = 1;
    cmd.w = 1;
    BATS(uint4, pushConsts.cmdBufferId, 0, cmd);
}

#if DDGI_BLEND_DIM_IRRADIANCE
    #define PROBE_NUM_TEXELS kDDGIProbeIrradianceTexelNum
    groupshared float3 sRadiance[kDDGIPerProbeRayCount];
#else 
    #define PROBE_NUM_TEXELS kDDGIProbeDistanceTexelNum  
#endif

groupshared float sDistance[kDDGIPerProbeRayCount];
groupshared float3 sRayDirection[kDDGIPerProbeRayCount];

// For irradiance: 6x6 with 1 border, is 8x8
// For distance: 14x14 with 1 border, is 16x16
[numthreads(PROBE_NUM_TEXELS, PROBE_NUM_TEXELS, 1)]
void mainCS(
    uint3 dispatchThreadID : SV_DispatchThreadID,
    uint3 groupThreadID    : SV_GroupThreadID,
    uint3 groupID          : SV_GroupID,
    uint  groupIndex       : SV_GroupIndex)
{ 
    // 
    const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, pushConsts.ddgiConfigId);

    const uint linearProbeIndex = BATL(uint, pushConsts.probeTraceLinearIndexSRV, groupID.x);
    const int3 virtualProbeIndex = ddgiConfig.getVirtualVolumeId(linearProbeIndex);  

    // 
    const int3 physicsProbeIndex = ddgiConfig.getPhysicalVolumeId(virtualProbeIndex);
    const int physicsProbeLinearIndex = ddgiConfig.getPhysicalLinearVolumeId(virtualProbeIndex);

    // Valid check. 
    const int probeState = BATL(int, pushConsts.probeTracedMarkSRV, physicsProbeLinearIndex);
    const int probeHistoryValid = RWBATL(int, pushConsts.probeHistoryValidUAV, physicsProbeLinearIndex);

    // 
#if DDGI_BLEND_DIM_IRRADIANCE
    check(probeState >= 0);
#else
    check(probeState == 0);
#endif

    // Load cache probe info.
    DDGIProbeCacheTraceInfo probeCacheTraceInfo = BATL(DDGIProbeCacheTraceInfo, pushConsts.probeCacheInfoSRV, physicsProbeLinearIndex);

    for (int rayIndex = groupIndex; rayIndex < kDDGIPerProbeRayCount; rayIndex ++)
    {
        int raytStorePosition = physicsProbeLinearIndex * kDDGIPerProbeRayCount + rayIndex;

        // Store mini gbuffer.
        DDGIProbeCacheMiniGbuffer miniGbuffer = BATL(DDGIProbeCacheMiniGbuffer, pushConsts.probeCacheRayGbufferSRV, raytStorePosition);
        sDistance[rayIndex] = miniGbuffer.distance;

        // 
        float3 rayDirection = ddgiConfig.getSampleRayDir(rayIndex);
        rayDirection = mul(probeCacheTraceInfo.rayRotation, float4(rayDirection, 0.0)).xyz;

        // 
        sRayDirection[rayIndex] = rayDirection;

    #if DDGI_BLEND_DIM_IRRADIANCE
        sRadiance[rayIndex] = BATL(float3, pushConsts.probeRadianceSRV, raytStorePosition);
    #endif
    }

    const bool bHistoryValid = ddgiConfig.bHistoryValid && (probeHistoryValid != 0);
    GroupMemoryBarrierWithGroupSync();

#if DDGI_BLEND_DIM_IRRADIANCE
    if (groupIndex == 0)
    {
        // Mark it's history valid post relighting.
        BATS(int, pushConsts.probeHistoryValidUAV, physicsProbeLinearIndex, 1);
    }
#endif

    // 
    const int2 probeTexelPos = ddgiConfig.getProbeTexelIndex(physicsProbeIndex, PROBE_NUM_TEXELS) + groupThreadID.xy;

    // Current thread is border texel or not.
    const bool bBorderTexel = 
        (groupThreadID.x == 0 || groupThreadID.x == (PROBE_NUM_TEXELS - 1)) || 
        (groupThreadID.y == 0 || groupThreadID.y == (PROBE_NUM_TEXELS - 1));

    if (!bBorderTexel)
    {
        // Initialize the max probe hit distance to 50% larger the maximum distance between probe grid cells
        const float probeMaxRayDistance = length(ddgiConfig.probeSpacing) * 1.5f;

        // 
        float4 result = 0.0; 

        // Current thread id remap to oct coord. 
        const float2 octCoord = ((groupThreadID.xy - 1) + 0.5) * rcp(PROBE_NUM_TEXELS - 2.0) * 2.0 - 1.0;
        const float3 texelDirection = octahedralDecode(octCoord);

        // Irradiance accumulate.
        for (int rayIndex = 0; rayIndex < kDDGIPerProbeRayCount; rayIndex ++) 
        {
            float3 rayDirection = sRayDirection[rayIndex];
 
            // Cosine weight when project to octahedron. 
            float weight = max(0, dot(rayDirection, texelDirection));

            // 
            float1 rayHitDistance = sDistance[rayIndex];

        #if DDGI_BLEND_DIM_IRRADIANCE
            // Back face hit.
            if (rayHitDistance < 0.0)
            {
                // Backface hits are ignored when blending radiance
                continue;
            }

            // 
            float3 rayHitRadiance = sRadiance[rayIndex];

            // Accumulate
            result += float4(rayHitRadiance * weight, weight);
        #else  
            // Filter distance weight. 
            weight = pow(weight, ddgiConfig.probeDistanceExponent); 

            // 
            rayHitDistance = min(abs(rayHitDistance), probeMaxRayDistance);

            // 
            result.x += rayHitDistance * weight;
            result.y += rayHitDistance * rayHitDistance * weight;
            result.w += weight;
        #endif 
        }

        result.xyz = (result.w > kFloatEpsilon) ? (result.xyz / result.w) : 0.0;
        result.w = 1.0f;

    #if DDGI_BLEND_DIM_IRRADIANCE
        // result.xyz = fastTonemap(result.xyz);

        float3 historyIrradiance = 0.0;
        if (bHistoryValid)
        {
            historyIrradiance = loadRWTexture2D_float3(pushConsts.UAV, probeTexelPos);
        }

        // Only do history lerp when history no zero. 
        float hysteresis = ddgiConfig.hysteresis;
        if (dot(historyIrradiance, 1.0) < kFloatEpsilon) 
        {
            hysteresis = 0.0;
        } 

        result.xyz = lerp(result.xyz, historyIrradiance, hysteresis);
        //
        storeRWTexture2D_float3(pushConsts.UAV, probeTexelPos, result.xyz);
    #else 
        if (bHistoryValid) 
        {
            float2 historyDistance = loadRWTexture2D_float2(pushConsts.UAV, probeTexelPos);
            result.rg = lerp(result.rg, historyDistance, ddgiConfig.hysteresis);
        }

        //
        storeRWTexture2D_float2(pushConsts.UAV, probeTexelPos, result.rg);
    #endif
    } 

    AllMemoryBarrierWithGroupSync();

    if (bBorderTexel)
    {
        bool bCornerTexel = ((groupThreadID.x == 0) || (groupThreadID.x == (PROBE_NUM_TEXELS - 1))) && ((groupThreadID.y == 0) || (groupThreadID.y == (PROBE_NUM_TEXELS - 1)));
        bool bRowTexel = (groupThreadID.x > 0) && (groupThreadID.x < (PROBE_NUM_TEXELS - 1));
    //  bool bColumTexel = !bCornerTexel && !bRowTexel && bBorderTexel;

        int2 srcPixelCoord = probeTexelPos;
        if (bCornerTexel) 
        {
            srcPixelCoord += (PROBE_NUM_TEXELS - 2) * select(groupThreadID.xy == 0, 1, -1); 
        }
        else if(bRowTexel)
        {
            srcPixelCoord.x += (PROBE_NUM_TEXELS - 1) - groupThreadID.x;
            srcPixelCoord.y += (groupThreadID.y > 0) ? -1 : 1;
        }
        else // bColumTexel
        {
            srcPixelCoord.x += (groupThreadID.x > 0) ? -1 : 1;
            srcPixelCoord.y += (PROBE_NUM_TEXELS - 1) - groupThreadID.y;
        }

    #if DDGI_BLEND_DIM_IRRADIANCE
        float3 copyIrradiance = loadRWTexture2D_float3(pushConsts.UAV, srcPixelCoord);
        storeRWTexture2D_float3(pushConsts.UAV, probeTexelPos, copyIrradiance);
    #else 
        float2 copyDistance = loadRWTexture2D_float2(pushConsts.UAV, srcPixelCoord);
        storeRWTexture2D_float2(pushConsts.UAV, probeTexelPos, copyDistance);
    #endif
    }
}

#endif //!__cplusplus