#include "gi.h"

struct GIWorldProbeSHUpdatePushConsts
{
    uint cameraViewId;
    uint clipmapConfigBufferId;
    uint clipmapLevel;
    uint sh_uav;
    uint bLastCascade;
};
CHORD_PUSHCONST(GIWorldProbeSHUpdatePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void mainCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // Get world probe linear index. 
    const uint world_probeLinearIndex = workGroupId * 64 + localThreadIndex;

    // Load probe config. 
    GIWorldProbeVolumeConfig config = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, pushConsts.clipmapLevel);

    if (world_probeLinearIndex >= config.getProbeCount())
    {
        return;
    }

    // 
    int3 world_probeCellId = config.getVirtualVolumeId(world_probeLinearIndex);
    int world_probePhysicsId = config.getPhysicalLinearVolumeId(world_probeCellId);

    SH3_gi world_gi_sh;
    if (config.isHistoryValid(world_probeCellId))
    {
        SH3_gi_pack sh_pack = BATL(SH3_gi_pack, config.sh_SRV, world_probePhysicsId);
        world_gi_sh.unpack(sh_pack);
    }
    else
    {
        world_gi_sh.init();
    }

    if (world_gi_sh.numSample <= 0.0)
    {
        // Try to stole SH from next cascade. 
        if (!pushConsts.bLastCascade && !config.bResetAll)
        {
            // 
            float3 positionRS = config.getPositionRS(world_probeCellId);

            // Just point sample. 
            GIWorldProbeVolumeConfig nextConfig = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, pushConsts.clipmapLevel + 1);
            const int3 next_baseProbeCoords = nextConfig.getVirtualVolumeIdFromPosition(positionRS);
            int next_world_probePhysicsId   = nextConfig.getPhysicalLinearVolumeId(next_baseProbeCoords);

        #if 1
            SH3_gi_pack sh_pack = BATL(SH3_gi_pack, nextConfig.sh_SRV, next_world_probePhysicsId);
            world_gi_sh.unpack(sh_pack);
        #else 
            nextConfig.sampleSH(perView, positionRS, world_gi_sh); 
        #endif

            world_gi_sh.numSample = min(world_gi_sh.numSample, 1.0f);
        }
    } 

    if (world_gi_sh.isNaN() || config.bResetAll || perView.bCameraCut)
    {
        world_gi_sh.init();
    }

    // 
    BATS(SH3_gi_pack, pushConsts.sh_uav, world_probePhysicsId, world_gi_sh.pack());
}

#endif //  