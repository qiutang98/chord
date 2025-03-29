#ifndef SHADER_AUTO_EXPOSURE
#define SHADER_AUTO_EXPOSURE

#include "base.h"

struct AutoExposurePushConsts
{
    // 
    uint SRV_histogram;

    //
    uint SRV_historyExposure;
    uint UAV_exposure;

    // https://www.desmos.com/calculator/qeussmkzlv
    float a;
    float b;

    float lowPercentage;
    float highPercentage;
    float minBrightness;
    float maxBrightness;

    float compensation;
    float speedDown;
    float speedUp;
    float deltaTime;

    float fixExposure;
};
CHORD_PUSHCONST(AutoExposurePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "colorspace.h"

groupshared uint sHistogramMax;
groupshared uint sHistogramSum;

groupshared uint sHistogram[kHistogramBinCount];
groupshared float sAcesCgLum[kHistogramBinCount];

float interpolateExposure(float newExposure, float oldExposure)
{
    float delta = newExposure - oldExposure;
    float speed = delta > 0.0 ? pushConsts.speedDown : pushConsts.speedUp;

    // Time delta from https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
    return oldExposure + delta * saturate(1.0 - exp2(-pushConsts.deltaTime * speed));
}

[numthreads(kHistogramBinCount, 1, 1)]
void mainCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    uint sampleHistogram = BATL(uint, pushConsts.SRV_histogram, localThreadIndex);
    sHistogram[localThreadIndex] = sampleHistogram;

    const float normalizeBinIdx = float(localThreadIndex) / (kHistogramBinCount - 1); // [0.0, 1.0]
    sAcesCgLum[localThreadIndex] = exp2((normalizeBinIdx - pushConsts.b) / pushConsts.a);

    // 
    if (localThreadIndex == 0)
    {
        sHistogramMax = 0;
        sHistogramSum = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint histogramMax_wave = WaveActiveMax(sampleHistogram);
    uint histogramSum_wave = WaveActiveSum(sampleHistogram);

    if (WaveIsFirstLane())
    {
        InterlockedMax(sHistogramMax, histogramMax_wave);
        InterlockedAdd(sHistogramSum, histogramSum_wave);
    }

    GroupMemoryBarrierWithGroupSync();

    if (localThreadIndex == 0)
    {
        uint histogramMax = sHistogramMax;
        uint histogramSum = sHistogramSum;

        const float normalizeHistogramFactor = 1.0 / float(histogramMax); // Max-> 1.0, other [0.0, 1.0)
        const float normalizeHistogramSum = normalizeHistogramFactor * float(histogramSum);

        float4 filterResult = 0.0;
        filterResult.z = normalizeHistogramSum * pushConsts.lowPercentage;
        filterResult.w = normalizeHistogramSum * pushConsts.highPercentage;

        // Stable filter inspired from unity HDRP eye adaption.
        for (uint i = 0; i < kHistogramBinCount; i ++)
        {
            float normalizeSample = sHistogram[i] * normalizeHistogramFactor;

            // Filter dark areas
            float offset = min(filterResult.z, normalizeSample);
            normalizeSample -= offset; // When current sample darker than avg, remove this sample.
            filterResult.zw -= offset;

            // Filter highlights
            normalizeSample = min(filterResult.w, normalizeSample);
            filterResult.w -= normalizeSample;

            filterResult.xy += float2(sAcesCgLum[i] * normalizeSample, normalizeSample);
        }

        float averageLumiance = clamp(filterResult.x / max(filterResult.y, 1e-4), pushConsts.minBrightness, pushConsts.maxBrightness);
        averageLumiance = max(1e-4, averageLumiance);

    #if 0
        // https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
        float keyValue = 1.03 - (2.0 / (2.0 + log2(averageLumiance + 1.0)));
        keyValue += pushConsts.compensation;
    #else
        float keyValue = pushConsts.compensation;
    #endif
        float exposure = keyValue / averageLumiance;

        if (pushConsts.SRV_historyExposure != kUnvalidIdUint32)
        {
            float prev_exposure = BATL(float, pushConsts.SRV_historyExposure, 0);
            exposure = interpolateExposure(exposure, prev_exposure);
        }

        if (pushConsts.fixExposure > 0.0)
        {
            exposure = pushConsts.fixExposure;
        }

        if (isnan(exposure) || isinf(exposure) || exposure < 1e-6f)
        {
            exposure = 1.0f;
        }

        BATS(float, pushConsts.UAV_exposure, 0, exposure);
    }
}


#endif 

#endif // SHADER_AUTO_EXPOSURE