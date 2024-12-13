#include "base.h"

struct DebugBlitPushConsts
{
    uint2  dimension;

    uint pointClampSamplerId;

    uint SRV;
    uint UAV;
};
CHORD_PUSHCONST(DebugBlitPushConsts, pushConsts);


#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 
#include "fullscreen.hlsl"
#include "blue_noise.hlsli"
#include "aces.hlsli"


[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    SamplerState pointClampSampler = Bindless(SamplerState, pushConsts.pointClampSamplerId);

    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);
    if (any(workPos >= pushConsts.dimension))
    {
        return;
    }

    float2 uv = (workPos + 0.5) / pushConsts.dimension;
    float4 input = sampleTexture2D_float4(pushConsts.SRV, uv, pointClampSampler);

    if (any(isnan(input)))
    {
        input = 0.0;
    }

    storeRWTexture2D_float4(pushConsts.UAV, workPos, input);
}

#endif 