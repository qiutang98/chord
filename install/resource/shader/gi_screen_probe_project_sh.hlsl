// Screen probe project to screen SH. 

#include "gi.h"

struct GIScreenProbeProjectSHPushConsts
{
    uint2 probeDim;
    uint2 gbufferDim;

    uint cameraViewId;
    uint probeSpawnInfoSRV;
    uint radianceSRV;
    uint screenProbeSHUAV;
};
CHORD_PUSHCONST(GIScreenProbeProjectSHPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

groupshared half3 sProbeSH[9 * 64];

uint shLdsIndexCompute(uint2 id, uint shIndex)
{
    uint flattenIndex = id.x + id.y * 8;
    return flattenIndex * 9 + shIndex;
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
        const uint4 packProbeSpawnInfo = BATL(uint4, pushConsts.probeSpawnInfoSRV, probeLinearIndex);
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
        float2 probeUv = (spawnInfo.pixelPosition + 0.5) / pushConsts.gbufferDim;
        probePositionRS = getPositionRS(probeUv, spawnInfo.depth, perView); 
    }  
    probePositionRS = WaveReadLaneFirst(probePositionRS);
    float3 rayDirection = getScreenProbeCellRayDirection(scene, probeCoord, gid, probeNormalRS);

    // Load radiance. 
    float3 radiance = loadTexture2D_float3(pushConsts.radianceSRV, tid);

    // 
    float directionSH[9]; 
    sh3_coefficients(rayDirection, directionSH);
 
    //  
    float NoL = dot(rayDirection, probeNormalRS);
    for (uint i = 0; i < 9; i ++)
    {
        sProbeSH[shLdsIndexCompute(gid, i)] = half3(NoL * radiance * directionSH[i]);
    }
    
    GroupMemoryBarrierWithGroupSync();

    // Sum reduce.
    for (uint shIndex = 0; shIndex < 9; shIndex ++)
    {
        for (uint i = 0; i < 3; i ++)
        {
            uint stride = 1u << (i + 1u);
            if (all(gid < 8 / stride))
            {
                half3 a00 = sProbeSH[shLdsIndexCompute(gid * stride + uint2(         0,          0), shIndex)];
                half3 a10 = sProbeSH[shLdsIndexCompute(gid * stride + uint2(stride / 2,          0), shIndex)];
                half3 a01 = sProbeSH[shLdsIndexCompute(gid * stride + uint2(         0, stride / 2), shIndex)];
                half3 a11 = sProbeSH[shLdsIndexCompute(gid * stride + uint2(stride / 2, stride / 2), shIndex)];

                sProbeSH[shLdsIndexCompute(gid * stride, shIndex)] = a00 + a01 + a10 + a11;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();
   
    if (localThreadIndex < 9)
    {
        half3 sh = sProbeSH[shLdsIndexCompute(uint2(0, 0), localThreadIndex)];

        uint storePos = probeLinearIndex * 9 + localThreadIndex;
        BATS(float3, pushConsts.screenProbeSHUAV, storePos, float3(sh)); // TODO: PACK SH.
    }
}

#endif 