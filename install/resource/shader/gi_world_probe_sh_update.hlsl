#include "gi.h"

struct GIWorldProbeSHUpdatePushConsts
{
    uint cameraViewId;
    uint clipmapConfigBufferId;
    uint clipmapCount;
};
CHORD_PUSHCONST(GIWorldProbeSHUpdatePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void mainCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // 
    check(64 % pushConsts.clipmapCount == 0); 

    uint cascadeId = localThreadIndex % pushConsts.clipmapCount;
    uint localProbeOffset = localThreadIndex / pushConsts.clipmapCount;
    uint workGroupProbeCount = 64 / pushConsts.clipmapCount; // How many probe can handle per work group, included all cascacdes.

    // 
    const bool bLastCascade = (cascadeId == (pushConsts.clipmapCount - 1));

    // Get world probe linear index. 
    const uint world_probeLinearIndex = workGroupId * workGroupProbeCount + localProbeOffset;

    // Load probe config. 
    GIWorldProbeVolumeConfig config = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, cascadeId);

    if (world_probeLinearIndex >= config.getProbeCount())
    {
        return;
    }

    // 
    int3 world_probeCellId = config.getVirtualVolumeId(world_probeLinearIndex);
    int world_probePhysicsId = config.getPhysicalLinearVolumeId(world_probeCellId);

    SH3_gi world_gi_sh;
    bool bShouldUpdate = false;
    if (config.isHistoryValid(world_probeCellId))
    {
        SH3_gi_pack sh_pack = RWBATL(SH3_gi_pack, config.sh_UAV, world_probePhysicsId);
        world_gi_sh.unpack(sh_pack);
    }
    else
    {
        world_gi_sh.init();
        bShouldUpdate = true;
    }

    if (world_gi_sh.numSample <= 0.0)
    {
        // Try to stole SH from next cascade. 
        if (!bLastCascade && !config.bResetAll)
        {
            // 
            float3 positionRS = config.getPositionRS(world_probeCellId);

            // Just point sample. 
            GIWorldProbeVolumeConfig nextConfig = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, cascadeId + 1);
            const int3 next_baseProbeCoords = nextConfig.getVirtualVolumeIdFromPosition(positionRS);
            int next_world_probePhysicsId   = nextConfig.getPhysicalLinearVolumeId(next_baseProbeCoords);

        #if 1
            SH3_gi_pack sh_pack = RWBATL(SH3_gi_pack, nextConfig.sh_UAV, next_world_probePhysicsId);
            world_gi_sh.unpack(sh_pack);
        #else 
            nextConfig.sampleSH(perView, positionRS, world_gi_sh); 
        #endif

            bShouldUpdate = true;
        }
    } 

    if (world_gi_sh.isNaN() || config.bResetAll || perView.bCameraCut)
    {
        world_gi_sh.init();
        bShouldUpdate = true;
    }

    if (bShouldUpdate)
    {
        BATS(SH3_gi_pack, config.sh_UAV, world_probePhysicsId, world_gi_sh.pack());
    }
}

#endif //  