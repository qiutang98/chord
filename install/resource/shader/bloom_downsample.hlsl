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
    float brightness = max(c.r, max(c.g, c.b));
    float soft = brightness - prefilterFactor.y;

    soft = clamp(soft, 0, prefilterFactor.z);
    soft = soft * soft * prefilterFactor.w;
    
    float contribution = max(soft, brightness - prefilterFactor.x);
    contribution /= max(brightness, 0.00001);

    return c * contribution;
}

// 13 tap downsample kernal.
static const uint kDownSampleCount = 13;
static const float2 kDownSampleCoords[kDownSampleCount] = 
{
    float2( 0.0,  0.0),
	float2(-1.0, -1.0), 
    float2( 1.0, -1.0), 
    float2( 1.0,  1.0),
    float2(-1.0,  1.0),
	float2(-2.0, -2.0),
    float2( 0.0, -2.0), 
    float2( 2.0, -2.0), 
    float2( 2.0,  0.0), 
    float2( 2.0,  2.0), 
    float2( 0.0,  2.0), 
    float2(-2.0,  2.0), 
    float2(-2.0,  0.0)
};

static const float kWeights[kDownSampleCount] = 
{
    0.125, // 0, 0
    0.125, // 1, 1
    0.125, // 1, 1
    0.125, // 1, 1
    0.125, // 1, 1
    0.03125, // 2, 2
    0.0625,  // 0, 2
    0.03125, // 2, 2
    0.0625,  // 
    0.03125, 
    0.0625, 
    0.03125, 
    0.0625  // 
};

static const int kDownSampleGroupCnt = 5;
static const int kSamplePerGroup = 4;
static const int kDownSampleGroups[kDownSampleGroupCnt][kSamplePerGroup] = 
{
	{ 1, 2,  3,  4},
    { 5, 6,  0, 12},
    { 6, 7,  8,  0},
    { 0, 8,  9, 10},
    {12, 0, 10, 11}
};
static const float kDownSampleGroupWeights[kDownSampleGroupCnt] = 
{
	0.5, 0.125, 0.125, 0.125, 0.125
};


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
    SamplerState pointSampler = getPointClampEdgeSampler(perView);

    float3 samples[kDownSampleCount]; 
    for (uint i = 0; i < kDownSampleCount; i ++)
    {
        float2 sampleUv = uv + kDownSampleCoords[i] * srcTexelSize;

        // When downsample, we should not use clamp to edge sampler.
        // Evaluate some bright pixel on the edge, if clamp to edge, down sample level edge pixel will capture it in multi sample.
        // And accumulate all of them then get a bright pixel.
        samples[i] = sampleTexture2D_float3(pushConsts.SRV, sampleUv, pointSampler);

    #if DIM_PASS_0
        samples[i] = prefilter(samples[i], pushConsts.prefilterFactor);
    #endif
    }

    float3 outColor = 0.0;

#if DIM_PASS_0
    float sampleKarisWeight[kDownSampleCount];
    for(uint i = 0; i < kDownSampleCount; i ++)
    {
        sampleKarisWeight[i] = 1.0 / (1.0 + ap1_luminance_f_rgb(samples[i]));
    }
    
    for(int i = 0; i < kDownSampleGroupCnt; i++)
    {
        float sumedKarisWeight = 0; 
        for(int j = 0; j < kSamplePerGroup; j++)
        {
            sumedKarisWeight += sampleKarisWeight[kDownSampleGroups[i][j]];
        }

        // Anti AA filter.
        for(int j = 0; j < kSamplePerGroup; j++)
        {
            outColor += kDownSampleGroupWeights[i] * sampleKarisWeight[kDownSampleGroups[i][j]] / sumedKarisWeight * samples[kDownSampleGroups[i][j]];
        }
    }
#else
    for (uint i = 0; i < kDownSampleCount; i ++)
    {
        outColor += samples[i] * kWeights[i];
    }
#endif

    storeRWTexture2D_float3(pushConsts.UAV, workPos, outColor);
}
#endif