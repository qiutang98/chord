#ifndef SHADER_GI_WORLD_PROBE_SH_PROPAGATE_HLSL
#define SHADER_GI_WORLD_PROBE_SH_PROPAGATE_HLSL

#include "gi.h"

struct GIWorldProbeSHPropagatePushConsts
{
    uint cameraViewId;
    uint clipmapConfigBufferId;
    uint clipmapLevel;
    uint sh_uav;

    uint sh_srv;

    //
    float energyLose;
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

    SH3_gi world_gi_sh;
    {
        SH3_gi_pack sh_pack = BATL(SH3_gi_pack, pushConsts.sh_srv, world_probePhysicsId);
        world_gi_sh.unpack(sh_pack);
    }

    //
    float weightSum = world_gi_sh.numSample * world_gi_sh.numSample;
    world_gi_sh.mul(weightSum);

    // Broadcast to fill whole voxel lighting.
    for (int z = -1; z <= 1; z ++)
    {
        for (int y = -1; y <= 1; y ++)
        {
            for (int x = -1; x <= 1; x ++)
            {
                int3 offsetCoord = int3(x, y, z);
                if (all(offsetCoord == 0))
                {
                    continue;
                }

                int3 sampleCoord = offsetCoord + world_probeCellId;
                if (any(sampleCoord < 0) || any(sampleCoord >= config.probeDim))
                {
                    continue;
                }

                int sample_probePhysicsId = config.getPhysicalLinearVolumeId(sampleCoord);
                
                SH3_gi sample_gi_sh;
                {
                    SH3_gi_pack sh_pack = BATL(SH3_gi_pack, pushConsts.sh_srv, sample_probePhysicsId);
                    sample_gi_sh.unpack(sh_pack);
                }

                // Skip too low sample probe. 
                if (sample_gi_sh.numSample < 1e-3f)
                {
                    continue;
                }

                float weight = 1.0 / dot(offsetCoord, offsetCoord);

                world_gi_sh.add(sample_gi_sh, weight);
                weightSum += weight;
            }
        }
    }

    if (weightSum < 1e-6f || perView.bCameraCut || config.bResetAll)
    {
        world_gi_sh.init();
    }
    else
    {
        // Normalize. 
        world_gi_sh.div(weightSum);
    }

    // Update energy loss. 
    world_gi_sh.numSample *= 0.9; // pushConsts.energyLose; //

    // 
    BATS(SH3_gi_pack, pushConsts.sh_uav, world_probePhysicsId, world_gi_sh.pack());
}

#endif //  

#endif // SHADER_GI_WORLD_PROBE_SH_PROPAGATE_HLSL