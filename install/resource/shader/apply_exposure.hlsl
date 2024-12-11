#include "base.h"

struct ApplyExposurePushConsts
{
    uint2 workDim;

    uint UAV;
    uint SRV_exposure;
};
CHORD_PUSHCONST(ApplyExposurePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "colorspace.h"
#include "base.hlsli"

[numthreads(256, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    uint2 bid = workGroupId * 32 + remap16x16(localThreadIndex);

    // Load exposure.
    float exposure = 0.0;
    if (WaveIsFirstLane())
    {
        exposure = BATL(float, pushConsts.SRV_exposure, 0);
    }
    exposure = WaveReadLaneFirst(exposure);

    if (isnan(exposure) || isinf(exposure) || exposure < 1e-6f)
    {
        exposure = 1.0f;
    }


    [unroll(2)]
    for (uint x = 0; x < 2; x ++)
    {
        [unroll(2)]
        for (uint y = 0; y < 2; y ++)
        {
            uint2 coord = bid + uint2(x, y) * 16;

            if (all(coord < pushConsts.workDim))
            {
                float3 color = exposure * loadRWTexture2D_float3(pushConsts.UAV, coord);
                storeRWTexture2D_float3(pushConsts.UAV, coord, color);
            }
        }
    }
}

#endif 