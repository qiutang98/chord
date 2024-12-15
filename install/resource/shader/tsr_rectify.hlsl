#include "base.h"
#include "colorspace.h"

// 3x3 neighbor sample pattern.
static const int2 kTSRRectifyNeighbourOffsets[9] =
{
    int2( 0,  0), // c
    int2( 0,  1),
    int2( 1,  0),
    int2(-1,  0),
    int2( 0, -1),
    int2(-1,  1),
    int2( 1, -1),
    int2( 1,  1), 
    int2(-1, -1),
};

struct TSRRecitfyPushConsts
{
    uint2 gbufferDim;

    uint cameraViewId; 
    uint hdrSceneColorId;
    uint closestMotionVectorId;
    uint reprojectedId;

    uint UAV;

    float blackmanHarrisWeights[9];
};
CHORD_PUSHCONST(TSRRecitfyPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "base.hlsli"
#include "bindless.hlsli"

// Color space: acescg -> srgb -> ycocg -> tonemapping. 
float3 colorSpaceConvert(float3 ap1)
{
    float3 srgb =  mul(AP1_2_sRGB, ap1);
    float3 ycocg = RGBToYCoCg_srgb(srgb);

    // simple tonemapping to (0, 1)
    return ycocg / (1.0 + ycocg.x); 
}

float3 colorSpaceInverse(float3 ye)
{
    // invert tonemapping. 
    float3 ycocg = ye / (1.0 - ye.x); 
    float3 srgb = YCoCgToRGB_srgb(ycocg);

    return mul(sRGB_2_AP1, srgb);
}

// 3x3 neighborhood samples.
struct NeighbourhoodSamples
{
    float3 neighbours[9];

    float3 minNeighbour;
    float3 maxNeighbour;
    float3 avgNeighbour; // Excluded center. 
};

void varianceClip(inout NeighbourhoodSamples samples, float historyLuma, float colorLuma, float velocityLengthInPixels, out float aggressiveClampedHistoryLuma)
{
    const float kAntiFlickerIntensity = 1.75f;
    const float kContrastForMaxAntiFlicker = 0.7f - lerp(0.0f, 0.3f, smoothstep(0.5f, 1.0f, 0.5f));

    // Prepare moments.
    float3 moment1 = 0.0;
    float3 moment2 = 0.0;

    [unroll(8)]
    for (int i = 1; i < 9; ++i)
    {
        moment1 += samples.neighbours[i];
        moment2 += samples.neighbours[i] * samples.neighbours[i];
    }
    samples.avgNeighbour = moment1 / 8.0f;

    // Also accumulate center.
    moment1 += samples.neighbours[0];
    moment2 += samples.neighbours[0] * samples.neighbours[0];

    // Average moments.
    moment1 /= 9.0f;
    moment2 /= 9.0f;

    // Get std dev.
    float3 stdDev = sqrt(abs(moment2 - moment1 * moment1));

    // Luma based anti filcker, from unity hdrp.
    float stDevMultiplier = 1.5;
    {
        float aggressiveStdDevLuma = stdDev.x * 0.5;

        aggressiveClampedHistoryLuma = clamp(historyLuma, moment1.x - aggressiveStdDevLuma, moment1.x + aggressiveStdDevLuma);
        float temporalContrast = saturate(abs(colorLuma - aggressiveClampedHistoryLuma) / max(max(0.15, colorLuma), aggressiveClampedHistoryLuma));

        const float maxFactorScale = 2.25f; // when stationary
        const float minFactorScale = 0.80f; // when moving more than slightly

        float localizedAntiFlicker = lerp(
            kAntiFlickerIntensity * minFactorScale, 
            kAntiFlickerIntensity * maxFactorScale, 
            saturate(1.0f - 2.0f * (velocityLengthInPixels)));

        stDevMultiplier += lerp(0.0, localizedAntiFlicker, smoothstep(0.05, kContrastForMaxAntiFlicker, temporalContrast));
        stDevMultiplier  = lerp(stDevMultiplier, 0.75, saturate(velocityLengthInPixels / 50.0f));
    }

    samples.minNeighbour = moment1 - stdDev * stDevMultiplier;
    samples.maxNeighbour = moment1 + stdDev * stDevMultiplier;
}

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const int2 gid = remap8x8(localThreadIndex);  
    const int2 tid = workGroupId * 8 + gid;

    if (any(tid >= pushConsts.gbufferDim))
    {
        return;
    }

    // Current frame color sample. 
    NeighbourhoodSamples neighborSamples;
    [unroll(9)]
    for (int i = 0; i < 9; i ++)
    {
        const int2 sampleId = clamp(tid + kTSRRectifyNeighbourOffsets[i], 0, pushConsts.gbufferDim - 1);
        float3 color = loadTexture2D_float3(pushConsts.hdrSceneColorId, sampleId);

        // 
        neighborSamples.neighbours[i] = colorSpaceConvert(color);
    }

    // Simple spatial filter (Optional)
    float3 filteredColor = 0.0;
    #if 1
    {
        float totalWeight = 0.0f;
        [unroll(9)]
        for (int i = 0; i < 9; ++i)
        {
            float w = pushConsts.blackmanHarrisWeights[i];

            filteredColor += neighborSamples.neighbours[i] * w;
            totalWeight += w;
        }

        filteredColor /= totalWeight;
    }
    #else 
    {
        filteredColor = neighborSamples.neighbours[0];
    }
    #endif 

    float2 pixel_uv = (tid + 0.5) / pushConsts.gbufferDim;
    float2 velocity = loadTexture2D_float2(pushConsts.closestMotionVectorId, tid);
    float2 pixel_uv_history = pixel_uv + velocity;

    // Sample history.
    float3 historyColor;
    if (all(pixel_uv_history > 0.0) && all(pixel_uv_history < 1.0))
    {
        historyColor = sampleTexture2D_float3(pushConsts.reprojectedId, pixel_uv, getPointClampEdgeSampler(perView));
        historyColor = colorSpaceConvert(historyColor);
    }
    else
    {
        historyColor = filteredColor;
    }

    // 
    const float colorLumiance = filteredColor.x;
    const float historyLumiance = historyColor.x;


    // 
    const float velocityLength = length(velocity);
    const float velocityLengthInPixels = velocityLength * length(pushConsts.gbufferDim);

    // 
    float aggressivelyClampedHistoryLuma = 0;
    varianceClip(neighborSamples, historyLumiance, colorLumiance, velocityLengthInPixels, aggressivelyClampedHistoryLuma);

    // Clip history with aabb.
    historyColor = clipAABB_compute(neighborSamples.minNeighbour, neighborSamples.maxNeighbour, historyColor, 0.0);

    // Compute blend factor.
    float blendFactor = colorLumiance; // lumiance based. 
    {
        // Velocity factor.
        blendFactor = lerp(blendFactor, saturate(2.0f * blendFactor), saturate(velocityLengthInPixels  / 50.0f));

        // 
        blendFactor = clamp(blendFactor, 0.03f, 0.98f);
    }


    // Lerp accumulation.
    float3 color = lerp(historyColor, filteredColor, blendFactor);
    color = clamp(colorSpaceInverse(color), 0.0, kMaxHalfFloat);

    // 
    storeRWTexture2D_float3(pushConsts.UAV, tid, color);
}

#endif 