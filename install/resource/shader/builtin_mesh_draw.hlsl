#include "base.h"

struct BuiltinMeshDrawPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(BuiltinMeshDrawPushConst);

    float3 color;
    uint cameraViewId;

    float3 offset; // Already consider translate world.
    float  scale;
};
CHORD_PUSHCONST(BuiltinMeshDrawPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 
#include "base.hlsli"
#include "debug.hlsli"

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 normal   : NORMAL0;
    [[vk::location(2)]] float2 uv       : TEXCOORD0;
};

struct VS2PS
{
    float4 positionHS : SV_Position;

    [[vk::location(0)]] float3 normal : NORMAL0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

VS2PS builtinMeshVS(VSInput input)
{
    VS2PS output = (VS2PS)0;

    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    float3 positionRS = input.position * pushConsts.scale + pushConsts.offset;
    output.positionHS = mul(perView.translatedWorldToClip, float4(positionRS, 1.0));

    output.normal     = input.normal;
    output.uv         = input.uv;

    return output;
}

void builtinMeshPS(in VS2PS input, out float4 outColor : SV_Target0)
{
    outColor.xyz = pushConsts.color;
    outColor.w   = 1.0;
}

#endif 