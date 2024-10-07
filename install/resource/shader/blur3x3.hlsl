#include "base.h"

struct Blur3x3PushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(Blur3x3PushConsts);

    uint2 dim;
    uint srv;
    uint uav;
};
CHORD_PUSHCONST(Blur3x3PushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 
#include "base.hlsli"

[numthreads(64, 1, 1)]
void blurShadowMask(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    int2 workPos = int2(workGroupId * 8 + remap8x8(localThreadIndex));
    Texture2D<float> shadowMaskTexture = TBindless(Texture2D, float, pushConsts.srv);

    float shadowMask[9];
    [unroll(4)]
    for (int i = 0; i < 4; i ++)
    {
        int2 samplePos = workPos + k3x3QuadSampleOffset[i] * k3x3QuadSampleSigned[i];
        samplePos = clamp(samplePos, 0, pushConsts.dim - 1);

        shadowMask[i] = shadowMaskTexture[samplePos];
    }

    shadowMask[4] =        QuadReadAcrossX(shadowMask[0]);
    shadowMask[5] =        QuadReadAcrossY(shadowMask[0]);
    shadowMask[6] = QuadReadAcrossDiagonal(shadowMask[0]);
    shadowMask[7] = QuadReadAcrossX(shadowMask[2]);
    shadowMask[8] = QuadReadAcrossY(shadowMask[3]);

    float shadowMaskSum = 0.0;
    [unroll(9)]
    for (int i = 0; i < 9; i ++)
    {
        shadowMaskSum += shadowMask[i];
    }

    if (all(workPos < pushConsts.dim))
    {
        RWTexture2D<float> screenShadowMask = TBindless(RWTexture2D, float, pushConsts.uav);
        screenShadowMask[workPos] = shadowMaskSum / 9.0f;
    }
}

#endif 