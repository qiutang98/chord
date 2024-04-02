#include "base.h"

struct ImGuiDrawPushConsts
{
    CHORD_DEFAULT_COMPARE(ImGuiDrawPushConsts);

    float2 scale;
    float2 translate;
    
    bool bFont;
    uint textureId;
    uint samplerId;
};
CHORD_PUSHCONST(ImGuiDrawPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 

struct VSIn 
{
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float2 uv  : TEXCOORD0;
    [[vk::location(2)]] float4 col : COLOR0; 

};

struct VS2PS
{
                        float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 uv  : TEXCOORD0;
    [[vk::location(1)]] float4 col : COLOR0;
};

VS2PS mainVS(VSIn input)
{
    VS2PS output;

    output.pos = float4(input.pos.xy * pushConsts.scale + pushConsts.translate, 0.f, 1.f);
    output.col = input.col;
    output.uv  = input.uv;

    return output;
};

float3 rec709GammaDecode(float3 gammaRec709)
{
    return lerp(gammaRec709 / 12.92, pow((gammaRec709 + .055) / 1.055, 2.4), step(0.04045, gammaRec709));
}

float3 rec709GammaEncode(float3 linearRec709)
{
    return lerp(12.92 * linearRec709, 1.055 * pow(linearRec709, 0.41666) - 0.055, step(0.0031308, linearRec709));
}


float4 mainPS(VS2PS input) : SV_Target
{
    Texture2D<float4> inputTexture = TBindless(Texture2D, float4, pushConsts.textureId);
    SamplerState inputSampler = Bindless(SamplerState, pushConsts.samplerId);
    
    float4 sampleColor = inputTexture.Sample(inputSampler, input.uv);
    if (pushConsts.bFont)
    {
        // Only r8 store all data.
        sampleColor = sampleColor.r;
    }
    else 
    {
        // We assume the input color can do correct color IO.
        // eg: linear rec709 store in unorm, srgb store in _srgb, so the hardware can convert color correctly. 
    }
    
    // ImGui color default in gamma rec709.
    float4 lerpColor = input.col;
    lerpColor.xyz = rec709GammaDecode(lerpColor.xyz);
    
    // Default in linearRec709, UI always draw in SRGB color buffer, so just output use hardware convert is fine.
    return lerpColor * sampleColor;
} 
   
#endif // HLSL only area end.