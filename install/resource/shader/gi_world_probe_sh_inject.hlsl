#ifndef SHADER_GI_WORLD_PROBE_SH_INJECT_HLSL
#define SHADER_GI_WORLD_PROBE_SH_INJECT_HLSL

#include "gi.h"

struct GIWorldProbeSHInjectPushConsts
{
    uint2 probeDim;
    uint2 gbufferDim;

    uint cameraViewId;
    uint probeSpawnInfoSRV;
    uint screenProbeSHSRV;

    uint clipmapConfigBufferId;
    uint clipmapCount; // 2 4 8
};
CHORD_PUSHCONST(GIWorldProbeSHInjectPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

// Loop all screen probe and inject world space sh probe. 
[numthreads(64, 1, 1)]
void mainCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // 
    check(64 % pushConsts.clipmapCount == 0); 

    // 
    uint cascadeId = localThreadIndex % pushConsts.clipmapCount;
    uint localProbeOffset = localThreadIndex / pushConsts.clipmapCount;
    uint workGroupProbeCount = 64 / pushConsts.clipmapCount; // How many probe can handle per work group, included all cascacdes.

    // Get probe linear index. 
    const uint screen_probeLinearIndex = workGroupId * workGroupProbeCount + localProbeOffset;

    // 
    if (screen_probeLinearIndex >= pushConsts.probeDim.x * pushConsts.probeDim.y)
    {
        return;
    }

    // Load spawn info.
    GIScreenProbeSpawnInfo spawnInfo;
    { 
        const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.probeSpawnInfoSRV, screen_probeLinearIndex);
        spawnInfo.unpack(packProbeSpawnInfo);
    }

    if (!spawnInfo.isValid())
    {
        return; // Skip invalid probe. 
    }

    float3 screen_probePositionRS = spawnInfo.getProbePositionRS(pushConsts.gbufferDim, perView); 

    // Load probe cascade config. 
    GIWorldProbeVolumeConfig config = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, cascadeId);

    float3 jitterOffset;
    {
        float3 xi = STBN_float3(scene.blueNoiseCtx, spawnInfo.pixelPosition, scene.frameCounter) * 2.0 - 1.0;
        jitterOffset = xi * config.probeSpacing;
    }

    // Load screen probe SH.
    SH3_gi screen_gi_sh;
    {
        SH3_gi_pack sh_pack = BATL(SH3_gi_pack, pushConsts.screenProbeSHSRV, screen_probeLinearIndex);
        screen_gi_sh.unpack(sh_pack);
    }

    // Blue noise jitter sample cell id.
    int3 cellId = config.getVirtualVolumeIdFromPosition(screen_probePositionRS + jitterOffset, false);
    if (any(cellId < 0) || any(cellId >= config.probeDim))
    {
        // Skip out of bound cell inject. 
        return;
    }

    int physicsCellIdx = config.getPhysicalLinearVolumeId(cellId);

    SH3_gi world_gi_sh;
    SH3_gi_pack sh_pack = RWBATL(SH3_gi_pack, config.sh_UAV, physicsCellIdx);
    world_gi_sh.unpack(sh_pack);

    const float maxNumSample = 64.0 * pow(1.3, cascadeId);
    float numSample = min(maxNumSample, world_gi_sh.numSample);

    // 
    float weight = 1.0 - 1.0 / (1.0 + numSample);

    // 
    bool bExistNaN = false;
    for (int i = 0; i < 9; i ++)
    {
        world_gi_sh.c[i] = lerp(screen_gi_sh.c[i], world_gi_sh.c[i], weight);
        bExistNaN = any(isnan(world_gi_sh.c[i]));
    }

    if (bExistNaN || config.bResetAll || perView.bCameraCut)
    {
        world_gi_sh.init();
        weight = 0.0;
    }
    else
    {
        world_gi_sh.numSample ++;
    }

    //
#if 0
    const float maxAccumulateSample = 8192.0 * pow(1.3, cascadeId);
    world_gi_sh.numSample = min(world_gi_sh.numSample, maxAccumulateSample);
#endif
    BATS(SH3_gi_pack, config.sh_UAV, physicsCellIdx, world_gi_sh.pack());
} 

#endif // 

#endif // SHADER_GI_WORLD_PROBE_SH_INJECT_HLSL