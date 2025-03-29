#ifndef SHADER_HISTOGRAM_HLSL
#define SHADER_HISTOGRAM_HLSL

#include "base.h"

struct HistogramPassPushConsts
{
    uint UAV;
    uint SRV;

    // https://www.desmos.com/calculator/qeussmkzlv
    float a;
    float b;
};
CHORD_PUSHCONST(HistogramPassPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "colorspace.h"
#include "base.hlsli"

groupshared uint sHistogram[kHistogramBinCount];

[numthreads(256, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    if (localThreadIndex < kHistogramBinCount)
    {
        sHistogram[localThreadIndex] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint2 bid = workGroupId * 32 + remap16x16(localThreadIndex);
    [unroll(2)]
    for (uint x = 0; x < 2; x ++)
    {
        [unroll(2)]
        for (uint y = 0; y < 2; y ++)
        {
            uint2 sampleCoord = bid + uint2(x, y) * 16;
            float3 acescg_rgb = loadTexture2D_float4(pushConsts.SRV, sampleCoord).xyz;
            float acescg_lum = ap1_luminance_f_rgb(acescg_rgb);

            // https://www.desmos.com/calculator/qeussmkzlv
            float log_lum = log2(acescg_lum) * pushConsts.a + pushConsts.b; // [0.0, 1.0]

            // 
            // check(log_lum >= 0.0 && log_lum <= 1.0); // Error value will cut some radiance.
            uint bin_idx = saturate(log_lum) * (kHistogramBinCount - 1);

            // 
            InterlockedAdd(sHistogram[bin_idx], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();
    if (localThreadIndex < kHistogramBinCount)
    {
        interlockedAddUint(pushConsts.UAV, sHistogram[localThreadIndex], localThreadIndex);
    }
}

#endif 

#endif // SHADER_HISTOGRAM_HLSL