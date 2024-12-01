#include "gi.h"

struct GIScreenProbeSpawnPushConsts
{
    uint2 probeDim;
    uint2 gbufferDim;

    uint cameraViewId;
    uint probeSpawnInfoUAV;

    uint depthSRV;
    uint normalRSId;
};
CHORD_PUSHCONST(GIScreenProbeSpawnPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

// Debug macros.
#define DEBUG_NORMAL_PACK 0

[[vk::binding(0, 1)]] RaytracingAccelerationStructure topLevelAS;

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // 
    const uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);
    if (any(workPos >= pushConsts.probeDim))
    {
        return;
    }

    const uint2 probeScreenOffset = workPos * 8;
    const uint2 probeCoord = workPos;

    uint2 seedPixel = pushConsts.probeDim;
    float depth = 0.0;
    for (int i = 0; i < 8; i ++)
    { 
        uint hashSeed = pcgHash(scene.frameCounter) + pcgHash(probeScreenOffset.x + pcgHash(probeScreenOffset.y + pcgHash(i)));
        float2 seedJitter = hammersley2d(hashSeed & 63, 64);

        uint2 jitterPixel = probeScreenOffset + seedJitter * 8;
        if (any(jitterPixel >= pushConsts.gbufferDim))
        {
            continue;
        }

        float jitterPixelDepth = loadTexture2D_float1(pushConsts.depthSRV, jitterPixel);

        if (jitterPixelDepth <= 0.0)
        {
            // Skip sky pixel. 
            continue;
        } 

        // Used first non sky pixel to spawn screen probe.  
        seedPixel = jitterPixel;
        depth = jitterPixelDepth;
        break;
    }

    const uint probeLinearIndex =  probeCoord.x + probeCoord.y * pushConsts.probeDim.x;

    GIScreenProbeSpawnInfo spawnResult; 
    spawnResult.init(); 

    if (depth > 0.0)
    {
        float3 normalRS = loadTexture2D_float4(pushConsts.normalRSId, seedPixel).xyz * 2.0 - 1.0;
        normalRS = normalize(normalRS);

        spawnResult.normalRS = normalRS;
        spawnResult.pixelPosition = seedPixel;   
        spawnResult.depth = depth; 

    #if 1
        // Sometime nanite mesh will produce large bias mesh surface. 
        // Ray trace get accurate hit t. 

        // Current direction exist mesh, shot one ray to query hit t to get perfect depth value. 
        float3 worldDirectionRS = spawnResult.getShootRayDir(pushConsts.gbufferDim, perView);

        GIRayQuery query;
        RayDesc ray = getRayDesc(0, worldDirectionRS, 0.0, kDefaultRayQueryTMax);
        const uint traceFlag = RAY_FLAG_CULL_NON_OPAQUE;
        query.TraceRayInline(topLevelAS, traceFlag, 0xFF, ray);
        query.Proceed();

        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            spawnResult.depth = -query.CommittedRayT();
        }
    #endif
    }
 
    uint4 storeResult = spawnResult.pack();

#if DEBUG_NORMAL_PACK
    float3 oldNormal = spawnResult.normalRS;
    { 
        spawnResult.unpack(storeResult);
        if (any(abs(oldNormal - spawnResult.normalRS) > 0.1))
        {
            printf("%f, %f, %f; %f, %f, %f", oldNormal.x, oldNormal.y, oldNormal.z, spawnResult.normalRS.x, spawnResult.normalRS.y, spawnResult.normalRS.z);
        }
    }
#endif

    BATS(uint4, pushConsts.probeSpawnInfoUAV, probeLinearIndex, storeResult);
}

#endif 