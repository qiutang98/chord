#ifndef SHADER_BLOOM_UPSAMPLE_HLSL
#define SHADER_BLOOM_UPSAMPLE_HLSL

#include "base.h"

struct BloomUpsamplePushConsts
{
    uint2 workDim;
    uint2 srcDim;

    float2 direction;
    float blurRadius; // 
    uint SRV;

    uint UAV;
    uint cameraViewId; //
    float radius; //
    float sigma; //
};
CHORD_PUSHCONST(BloomUpsamplePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "colorspace.h"
#include "base.hlsli"

float gaussianWeight(float x) 
{
    const float kSigma = pushConsts.sigma; 

    const float mu = 0; // From center.
    const float dx = x - mu;
    const float sigma2 = kSigma * kSigma;
    return 0.398942280401 / kSigma * exp(- (dx * dx) * 0.5 / sigma2);
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

    SamplerState pointSampler = getPointClampEdgeSampler(perView);

    float4 sum = 0.0;
    const float kRadius = pushConsts.radius;
    for(float i = -kRadius; i <= kRadius; i++)
    {
        float weight = gaussianWeight(i / kRadius);
        float2 sampleUv = uv + i * pushConsts.direction * srcTexelSize;

        sum.xyz += weight * sampleTexture2D_float3(pushConsts.SRV, sampleUv, pointSampler);
        sum.w   += weight;
    }

    // Normalize.
    sum.xyz /= sum.w;

#if DIM_COMPOSITE_UPSAMPLE
    float3 currentColor = loadRWTexture2D_float3(pushConsts.UAV, workPos);
//  sum.xyz = lerp(currentColor, sum.xyz, pushConsts.blurRadius);
    sum.xyz = currentColor + sum.xyz * pushConsts.blurRadius;  
#endif 

    // 
    storeRWTexture2D_float3(pushConsts.UAV, workPos, sum.xyz);
}


#endif // !__cplusplus

#endif // SHADER_BLOOM_UPSAMPLE_HLSL