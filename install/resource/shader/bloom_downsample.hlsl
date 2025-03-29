#ifndef SHADER_BLOOM_DOWNSAMPLE_HLSL
#define SHADER_BLOOM_DOWNSAMPLE_HLSL

#include "base.h"

struct BloomDownSamplePushConsts
{
    float4 prefilterFactor;

    uint2 workDim;
    uint2 srcDim;

    uint SRV;
    uint UAV;
    uint cameraViewId;
};
CHORD_PUSHCONST(BloomDownSamplePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "colorspace.h"
#include "base.hlsli"

// Bloom prefilter.
float3 prefilter(float3 c, float4 prefilterFactor) 
{
#if DIM_PASS_0
    // https://catlikecoding.com/unity/tutorials/advanced-rendering/bloom/
    float brightness = max(c.r, max(c.g, c.b));
    float soft = brightness - prefilterFactor.y;

    // 
    soft = clamp(soft, 0, prefilterFactor.z);
    soft = soft * soft * prefilterFactor.w;
    
    //
    float contribution = max(soft, brightness - prefilterFactor.x);
    contribution /= max(brightness, 0.00001);

    // 
    return c * contribution;
#else 
    // Don't do any operation if no first pass.
    return c;
#endif 
}

float getKarisWeight(float3 c)
{
    return 1.0 / (1.0 + ap1_luminance_f_rgb(c));
}

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);
    if (any(workPos >= pushConsts.workDim))
    {
        return;
    }

    float2 uv = (workPos + 0.5) / pushConsts.workDim; // Current working uv center.
    float2 srcTexelSize = 1.0 / pushConsts.srcDim;

    // 
    SamplerState linearSampler = getLinearClampEdgeSampler(perView);

    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    float3 a = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2(-2, -2), linearSampler), pushConsts.prefilterFactor);
    float3 b = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2( 0, -2), linearSampler), pushConsts.prefilterFactor);
    float3 c = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2( 2, -2), linearSampler), pushConsts.prefilterFactor);
    float3 d = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2(-2,  0), linearSampler), pushConsts.prefilterFactor);
    float3 e = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2( 0,  0), linearSampler), pushConsts.prefilterFactor);
    float3 f = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2( 2,  0), linearSampler), pushConsts.prefilterFactor);
    float3 g = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2(-2,  2), linearSampler), pushConsts.prefilterFactor);
    float3 h = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2( 0,  2), linearSampler), pushConsts.prefilterFactor);
    float3 i = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2( 2,  2), linearSampler), pushConsts.prefilterFactor);
    float3 j = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2(-1, -1), linearSampler), pushConsts.prefilterFactor);
    float3 k = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2( 1, -1), linearSampler), pushConsts.prefilterFactor);
    float3 l = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2(-1,  1), linearSampler), pushConsts.prefilterFactor);
    float3 m = prefilter(sampleTexture2D_float3(pushConsts.SRV, uv + srcTexelSize * int2( 1,  1), linearSampler), pushConsts.prefilterFactor);

    float3 outColor = 0.0;

#if DIM_PASS_0
    float3 c_0 = (a + b + d + e) * 0.25; c_0 *= getKarisWeight(c_0);
    float3 c_1 = (b + c + e + f) * 0.25; c_1 *= getKarisWeight(c_1);
    float3 c_2 = (d + e + g + h) * 0.25; c_2 *= getKarisWeight(c_2);
    float3 c_3 = (e + f + h + i) * 0.25; c_3 *= getKarisWeight(c_3);
    float3 c_4 = (j + k + l + m) * 0.25; c_4 *= getKarisWeight(c_4);
    
    outColor += 0.125 * (c_0 + c_1 + c_2 + c_3);
    outColor += 0.500 * (c_4                  );
#else
    outColor += 0.03125 * (a + c + g + i);
    outColor += 0.06250 * (b + d + f + h);
    outColor += 0.12500 * (j + k + l + m);
    outColor += 0.12500 * (e            );
#endif

    if (any(isnan(outColor)))
    {
        outColor = 0.0;
    }

    storeRWTexture2D_float3(pushConsts.UAV, workPos, outColor);
}
#endif // __cplusplus

#endif // SHADER_BLOOM_DOWNSAMPLE_HLSL