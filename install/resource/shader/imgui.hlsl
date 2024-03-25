#include "base.h"

struct ImGuiPushConsts 
{
    float4x4 projection;
    uint textureId;
    uint samplerId;
};
CHORD_PUSHCONST(ImGuiPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"

struct VSIn
{
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float4 col : COLOR0;
    [[vk::location(2)]] float2 uv  : TEXCOORD0;
};

struct VS2PS
{
                        float4 pos : SV_POSITION;
    [[vk::location(0)]] float4 col : COLOR0;
    [[vk::location(1)]] float2 uv  : TEXCOORD0;
};

VS2PS mainVS(VSIn input)
{
    VS2PS output;

    output.pos = mul(pushConsts.projection, float4(input.pos.xy, 0.f, 1.f));
    output.col = input.col;
    output.uv  = input.uv;

    return output;
};

float4 mainPS(VS2PS input) : SV_Target
{
    Texture2D<float4> inputTexture = TBindless(Texture2D, float4, pushConsts.textureId);
    SamplerState inputSampler = Bindless(SamplerState, pushConsts.samplerId);

    float4 result = input.col * inputTexture.Sample(inputSampler, input.uv); 
    return result; 
}

#endif // HLSL only area end.