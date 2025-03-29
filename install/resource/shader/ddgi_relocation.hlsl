#ifndef SHADER_DDGI_RELOCATION_HLSL
#define SHADER_DDGI_RELOCATION_HLSL

#include "ddgi.h"

struct DDGIRelocationPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(DDGIRelocationPushConsts);

    uint cameraViewId;
    uint ddgiConfigBufferId;
    uint ddgiConfigId;
    uint UAV; // probe offset.

    uint probeCounterSRV;
    uint probeTraceLinearIndexSRV;
    uint probeHistoryValidSRV;
    uint probeCacheRayGbufferSRV;

    uint probeCacheInfoSRV;
};
CHORD_PUSHCONST(DDGIRelocationPushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli"
#include "bindless.hlsli"
#include "sample.hlsli"
#include "debug.hlsli"

// Compute offset before probe trace. 
[numthreads(1, 1, 1)]
void indirectCmdParamCS()
{
    uint4 cmd;
    cmd.x = (31 + BATL(uint, pushConsts.probeCounterSRV, 1)) / 32;

    cmd.y = 1;
    cmd.z = 1;
    cmd.w = 1;
    BATS(uint4, pushConsts.UAV, 0, cmd);
}

[numthreads(32, 1, 1)]
void mainCS(uint dispatchId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, pushConsts.ddgiConfigId);
    const int probeCount = BATL(uint, pushConsts.probeCounterSRV, 1);
    

    if (dispatchId >= probeCount)
    {
        return;
    }

    int linearProbeIndex = BATL(uint, pushConsts.probeTraceLinearIndexSRV, dispatchId);
    const int3 virtualProbeIndex = ddgiConfig.getVirtualVolumeId(linearProbeIndex);  

    // 
    const int3 physicsProbeIndex = ddgiConfig.getPhysicalVolumeId(virtualProbeIndex);
    const int physicsProbeLinearIndex = ddgiConfig.getPhysicalLinearVolumeId(virtualProbeIndex);

    // History trace no valid so offset is zero. 
    const int probeHistoryValid = BATL(int, pushConsts.probeHistoryValidSRV, physicsProbeLinearIndex);
    if (probeHistoryValid == 0)
    {
        // Trick: slightly offset unit.
        BATS(float3, pushConsts.UAV, physicsProbeLinearIndex, float3(0.1, 0.1, 0.1)); // offset is zero if no valid history.
        return;
    }

    int closestBackfaceIndex = -1;
    float closestBackfaceDistance = 1e27f;
    int closestFrontfaceIndex = -1;
    float closestFrontfaceDistance = 1e27f;
    int farthestFrontfaceIndex = -1;
    float farthestFrontfaceDistance = 0.0;

    // 
    float backfaceCount = 0.0;

    // TODO: wave optimization.
    for (int rayBaseIndex = 0; rayBaseIndex < kDDGIPerProbeRayCount; rayBaseIndex ++) // rayBaseIndex += 4
    {
        // float4 gatherDistance4 = probeTraceTexture.GatherAlpha(pointClampSampler, loadPos * texelSize, int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1));

        // for (int rayOffset = 0; rayOffset < 4; rayOffset ++)
        {
            int rayIndex = rayBaseIndex; // + rayOffset;
            int raytStorePosition = physicsProbeLinearIndex * kDDGIPerProbeRayCount + rayIndex;

            DDGIProbeCacheMiniGbuffer miniGbuffer = BATL(DDGIProbeCacheMiniGbuffer, pushConsts.probeCacheRayGbufferSRV, raytStorePosition);
            float rayHitDistance = miniGbuffer.distance; // gatherDistance4[rayOffset];

            // back face hit
            if (rayHitDistance <= 0.0)
            {
                backfaceCount ++;

                // Get original distance. 
                rayHitDistance /= -0.2;

                // Update
                if (rayHitDistance < closestBackfaceDistance)
                {
                    closestBackfaceDistance = rayHitDistance;
                    closestBackfaceIndex = rayIndex;
                }
            }
            else
            {
                if (rayHitDistance < closestFrontfaceDistance)
                {
                    closestFrontfaceDistance = rayHitDistance;
                    closestFrontfaceIndex = rayIndex;
                }
                else if (rayHitDistance > farthestFrontfaceDistance)
                {
                    farthestFrontfaceDistance = rayHitDistance;
                    farthestFrontfaceIndex = rayIndex;
                }
            }
        }
    }

    // Current offset. 
    float3 offset = RWBATL(float3, pushConsts.UAV, physicsProbeLinearIndex);

    // 
    float3 fullOffset = 1e27f;
    const float probeRayBackFaceThreshold = 0.25f;

    // 
    DDGIProbeCacheTraceInfo probeCacheTraceInfo = BATL(DDGIProbeCacheTraceInfo, pushConsts.probeCacheInfoSRV, physicsProbeLinearIndex);
 
    // Nvidia DDGI offset algorithm. 
    if (closestBackfaceIndex != -1 && (backfaceCount / kDDGIPerProbeRayCount) > probeRayBackFaceThreshold)
    {
        float3 rayDirection = ddgiConfig.getSampleRayDir(closestBackfaceIndex);
        rayDirection = mul(probeCacheTraceInfo.rayRotation, float4(rayDirection, 0.0)).xyz;

        fullOffset = offset + rayDirection * (closestBackfaceDistance + ddgiConfig.probeMinFrontfaceDistance * 0.5);
    } 
    else if (closestFrontfaceDistance < ddgiConfig.probeMinFrontfaceDistance)
    {
        float3 closestFrontfaceDirection = ddgiConfig.getSampleRayDir(closestFrontfaceIndex);
        closestFrontfaceDirection = mul(probeCacheTraceInfo.rayRotation, float4(closestFrontfaceDirection, 0.0)).xyz;

        float3 farthestFrontfaceDirection = ddgiConfig.getSampleRayDir(farthestFrontfaceIndex);
        farthestFrontfaceDirection = mul(probeCacheTraceInfo.rayRotation, float4(farthestFrontfaceDirection, 0.0)).xyz;

        if (dot(closestFrontfaceDirection, farthestFrontfaceDirection) <= 0.f)
        {  
            // Ensures the probe never moves through the farthest frontface
            farthestFrontfaceDirection *= min(farthestFrontfaceDistance, 1.f);
            fullOffset = offset + farthestFrontfaceDirection; 
        }
    }
    else if (closestFrontfaceDistance > ddgiConfig.probeMinFrontfaceDistance)
    {
        // Probe isn't near anything, try to move it back towards zero offset
        float moveBackMargin = min(closestFrontfaceDistance - ddgiConfig.probeMinFrontfaceDistance, length(offset));
        float3 moveBackDirection = normalize(-offset);
        fullOffset = offset + (moveBackMargin * moveBackDirection);
    }

    // Absolute maximum distance that probe could be moved should satisfy ellipsoid equation:
    // x^2 / probeGridSpacing.x^2 + y^2 / probeGridSpacing.y^2 + z^2 / probeGridSpacing.y^2 < (0.5)^2
    // Clamp to less than maximum distance to avoid degenerate cases
    float3 normalizedOffset = fullOffset / ddgiConfig.probeSpacing;
    if (dot(normalizedOffset, normalizedOffset) < 0.2025f) // 0.45 * 0.45 == 0.2025
    {
        offset = fullOffset;
    }

    BATS(float3, pushConsts.UAV, physicsProbeLinearIndex, offset);
}

#endif // 

#endif // SHADER_DDGI_RELOCATION_HLSL