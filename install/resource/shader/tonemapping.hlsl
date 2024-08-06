#include "colorspace.h"

struct TonemappingPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(TonemappingPushConsts);

    uint textureId;
    uint pointClampSamplerId;
};
CHORD_PUSHCONST(TonemappingPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 
#include "fullscreen.hlsl"

void mainPS(
    in FullScreenVS2PS input, 
    out float4 outColor : SV_Target0)
{
    Texture2D<float4> inputTexture = TBindless(Texture2D, float4, pushConsts.textureId);
    SamplerState pointClampSampler = Bindless(SamplerState, pushConsts.pointClampSamplerId);

    float4 sampleColor = inputTexture.Sample(pointClampSampler, input.uv);







    sampleColor.xyz = pow(sampleColor.xyz, 2.2);






    outColor = sampleColor;
}

#endif