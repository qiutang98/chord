// Screen probe project to screen SH. 

#include "gi.h"

struct GIScreenProbeProjectSHPushConsts
{
    uint2 probeDim;
    uint2 gbufferDim;

    uint cameraViewId;
    uint probeSpawnInfoSRV;
    uint radianceSRV;
    uint shUAV;

    uint statSRV;
};
CHORD_PUSHCONST(GIScreenProbeProjectSHPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

groupshared uint sProbeSH[14 * 64];

void storeLDS(uint localThreadIndex, in SH3_gi giSH)
{
    SH3_gi_pack pack = giSH.pack();
    for (uint i = 0; i < 14; i ++)
    {
        sProbeSH[localThreadIndex * 14 + i] = pack.v[i];
    }
}

void loadLDS(uint localThreadIndex, inout SH3_gi giSH)
{
    SH3_gi_pack pack;
    for (uint i = 0; i < 14; i ++)
    {
        pack.v[i] = sProbeSH[localThreadIndex * 14 + i];
    }
    giSH.unpack(pack);
}

void reduceSH(bool bStoreLDS, uint localThreadIndex, uint rCount, inout SH3_gi giSH)
{
    if (localThreadIndex < rCount)
    {
        SH3_gi shResult_next;
        loadLDS(localThreadIndex + rCount, shResult_next);

        // 
        for (uint i = 0; i < 9; i ++)
        {
            giSH.c[i] += shResult_next.c[i];
        }

        if (bStoreLDS)
        {
            storeLDS(localThreadIndex, giSH);
        }
    }
}

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
 
    float3 probePositionRS;
    float3 probeNormalRS = spawnInfo.normalRS;


    if (WaveIsFirstLane())
    {
        probePositionRS = spawnInfo.getProbePositionRS(pushConsts.gbufferDim, perView); 
    }  
    probePositionRS = WaveReadLaneFirst(probePositionRS);
    float3 rayDirection = getScreenProbeCellRayDirection(scene, probeCoord, gid, probeNormalRS);

    // Load radiance. 
    float4 radianceHitT = loadTexture2D_float4(pushConsts.radianceSRV, tid);

    // 
    float3 radiance  = radianceHitT.xyz;

    // Dark area fallback.
    {
        float3 statRadiance = 0.0;
        if (WaveIsFirstLane()) 
        {
            if (pushConsts.statSRV != kUnvalidIdUint32)
            {
                statRadiance = loadTexture2D_float3(pushConsts.statSRV, probeCoord);
            }
        }
        statRadiance = WaveReadLaneFirst(statRadiance); 
        if (all(radiance < kFloatEpsilon))
        {
            radiance = statRadiance;
        }
    }

    // 
    SH3_gi shResult;
    shResult.init();

    // 
    SH_AddLightDirectional(shResult.c, radiance, rayDirection);
 
    // Store LDS
    storeLDS(localThreadIndex, shResult);
    
    // 
    GroupMemoryBarrierWithGroupSync();
    reduceSH(true, localThreadIndex, 32, shResult);
    GroupMemoryBarrierWithGroupSync();

    // Generic wave reduce. 
    reduceSH(true, localThreadIndex, 16, shResult);
    reduceSH(true, localThreadIndex,  8, shResult);
    reduceSH(true, localThreadIndex,  4, shResult);
    reduceSH(true, localThreadIndex,  2, shResult);

    // Final store to device memory.
    reduceSH(false, localThreadIndex, 1, shResult);
    if (localThreadIndex < 1)
    {
        shResult.numSample = 1.0;
        BATS(SH3_gi_pack, pushConsts.shUAV, probeLinearIndex, shResult.pack());
    }
}

#endif 