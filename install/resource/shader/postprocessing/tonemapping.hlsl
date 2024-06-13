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

    // todo:
    sampleColor *= 0.5f;

    outColor = sampleColor;
}

#endif