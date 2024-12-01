#include "gi.h"

struct GIWorldProbeSHPropagatePushConsts
{
    uint cameraViewId;
    uint clipmapConfigBufferId;
    uint clipmapLevel;
    uint sh_uav;
    uint sh_srv;
    bool bLastCascade;
};
CHORD_PUSHCONST(GIWorldProbeSHPropagatePushConsts, pushConsts);

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

    float3 world_probeDir;
    SH3_gi world_gi_sh;

    if (config.isHistoryValid(world_probeCellId))
    {
        SH3_gi_pack sh_pack = BATL(SH3_gi_pack, pushConsts.sh_srv, world_probePhysicsId);
        world_gi_sh.unpack(sh_pack);

        uint shState_pack = BATL(uint, config.sh_direction_SRV, world_probePhysicsId);
        world_probeDir = unpack_dir_f_uint(shState_pack);
    }
    else
    {
        world_probeDir = 0.0;
        world_gi_sh.init();
    }

    if (world_gi_sh.numSample <= 0.0)
    {
        // Try to stole SH from next cascade. 
        if (!pushConsts.bLastCascade && !config.bRestAll)
        {
            float3 positionRS = config.getPositionRS(world_probeCellId);

            // Just point sample. 
            GIWorldProbeVolumeConfig nextConfig = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, pushConsts.clipmapLevel + 1);
            const int3 next_baseProbeCoords = nextConfig.getVirtualVolumeIdFromPosition(positionRS);
            int next_world_probePhysicsId = nextConfig.getPhysicalLinearVolumeId(next_baseProbeCoords);

            uint shState_pack = BATL(uint, nextConfig.sh_direction_SRV, next_world_probePhysicsId);
            world_probeDir = unpack_dir_f_uint(shState_pack);

            SH3_gi_pack sh_pack = BATL(SH3_gi_pack, pushConsts.sh_srv, next_world_probePhysicsId);
            world_gi_sh.unpack(sh_pack);

            world_gi_sh.numSample = 0.0;
        }
    }

    if (world_gi_sh.numSample < kFloatEpsilon)
    {
        world_probeDir = 0.0;
    }

    world_gi_sh.numSample *= 0.99; // pow(0.8, pushConsts.clipmapLevel); // 
    
    BATS(SH3_gi_pack, pushConsts.sh_uav, world_probePhysicsId, world_gi_sh.pack());
}

#endif // 