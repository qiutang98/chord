#include "base.h"

// Reprojection history to current frame. 

struct TSRReprojectionPushConsts
{
    uint2 gbufferDim;
    uint cameraViewId; 
    uint closestMotionVectorId;

    uint historyColorId;
    uint UAV;
};
CHORD_PUSHCONST(TSRReprojectionPushConsts, pushConsts);


#ifndef __cplusplus // HLSL only area.

#include "base.hlsli"
#include "bindless.hlsli"

// TODO: Variant reproject dispatch.

float lanczos_2_approx_Weight(float x2) 
{
    float a = (2.0f / 5.0f) * x2 - 1;
    float b = (1.0f / 4.0f) * x2 - 1;
    return ((25.0f / 16.0f) * a * a - (25.0f / 16.0f - 1)) * (b * b);
}

float lanczosWeight(float2 x, float r) 
{
    return lanczos_2_approx_Weight(x.x * x.x) * lanczos_2_approx_Weight(x.y * x.y);
}

float3 sampleHistoryLanczos(float2 coord, int r)
{
    float2 res = pushConsts.gbufferDim;
    coord += -0.5 / res;
    float2 ccoord = floor(coord * res) / res;

    float3 total  = 0.0;   
    for (int x = -r; x <= r; x++) 
    {
        for (int y = -r; y <= r; y++) 
        {
            float2 offs = float2(x, y);
            
            float2 sco  = (offs / res) + ccoord;
            float2 d    = clamp((sco - coord) * res, -r, r);
            float3 val  = loadTexture2D_float3(pushConsts.historyColorId, sco * res);
            
            total += val * lanczosWeight(d, r);
        }
    }

    return total;
}

// Filmic SMAA presentation[Jimenez 2016]
float3 sampleHistoryBicubic5Tap(float2 uv, SamplerState linearSampler, float sharpening = 0.5f, bool bAntiRing = false)
{
    const float2 historySize = pushConsts.gbufferDim;
    const float2 historyTexelSize = 1.0f / historySize;

    // 
    float2 samplePos = uv * historySize;

    // 
    float2 tc1 = floor(samplePos - 0.5) + 0.5;
    float2 f  = samplePos - tc1;
    float2 f2 = f * f;
    float2 f3 = f * f2;

    // 
    const float c = sharpening;

    // 
    float2 w0  = -c * f3 +  2.0 * c * f2 - c * f; // f2 - 0.5 * (f3 + f);
    float2 w1  =  (2.0 - c) * f3 - (3.0 - c) * f2  + 1.0; // 1.5 * f3 - 2.5 * f2 + 1;
    float2 w2  = -(2.0 - c) * f3 + (3.0 - 2.0 * c)  * f2 + c * f; // 1 - w0 - w1 - w3;
    float2 w3  = c * f3 - c * f2; // 0.5 * (f3 - f2);
    float2 w12 = w1 + w2;

    // 
    float2 tc0  = historyTexelSize  * (tc1 - 1.0);
    float2 tc3  = historyTexelSize  * (tc1 + 2.0);
    float2 tc12 = historyTexelSize  * (tc1 + w2 / w12);

    // 
    float3 s0 = sampleTexture2D_float3(pushConsts.historyColorId, float2(tc12.x,  tc0.y), linearSampler);
    float3 s1 = sampleTexture2D_float3(pushConsts.historyColorId, float2(tc0.x,  tc12.y), linearSampler);
    float3 s2 = sampleTexture2D_float3(pushConsts.historyColorId, float2(tc12.x, tc12.y), linearSampler);
    float3 s3 = sampleTexture2D_float3(pushConsts.historyColorId, float2(tc3.x,   tc0.y), linearSampler);
    float3 s4 = sampleTexture2D_float3(pushConsts.historyColorId, float2(tc12.x,  tc3.y), linearSampler);

    float cw0 = (w12.x * w0.y);
    float cw1 = (w0.x  * w12.y);
    float cw2 = (w12.x * w12.y);
    float cw3 = (w3.x  * w12.y);
    float cw4 = (w12.x * w3.y);

    // Anti-ring from unity
    const float3 minColor = min(min(min(min(s0, s1), s2), s3), s4);
    const float3 maxColor = max(max(max(max(s0, s1), s2), s3), s4);

    s0 *= cw0;
    s1 *= cw1;
    s2 *= cw2;
    s3 *= cw3;
    s4 *= cw4;

    float3 historyFiltered = s0 + s1 + s2 + s3 + s4;
    float weightSum = cw0 + cw1 + cw2 + cw3 + cw4;

    // 
    float3 filteredVal = historyFiltered / weightSum;

    // Anti-ring from unity.
    // This sortof neighbourhood clamping seems to work to avoid the appearance of overly dark outlines in case
    // sharpening of history is too strong.
    if (bAntiRing)
    {
        filteredVal = clamp(filteredVal, minColor, maxColor);
    }

    // Final output clamp.
    return clamp(filteredVal, 0.0, kMaxHalfFloat);
}

[numthreads(256, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const int2 gid = remap16x16(localThreadIndex);
    const int2 tid = workGroupId * 16 + gid;

    float2 pixel_uv = (tid + 0.5) / pushConsts.gbufferDim;
    float2 pixel_uv_history = pixel_uv + loadTexture2D_float2(pushConsts.closestMotionVectorId, tid);

    float3 historyColor = 0.0;
    [branch]
    if (all(pixel_uv_history > 0.0) && all(pixel_uv_history < 1.0))
    {
        historyColor = sampleHistoryBicubic5Tap(pixel_uv_history, getLinearClampEdgeSampler(perView));
        // historyColor = sampleHistoryLanczos(pixel_uv_history, 2); 
    }
    else 
    {
        // History out of frame, stretch fill empty area. (point sample)
        historyColor = sampleTexture2D_float3(pushConsts.historyColorId, pixel_uv_history, getPointClampEdgeSampler(perView));
    }

    if (all(tid < pushConsts.gbufferDim))
    {
        storeRWTexture2D_float3(pushConsts.UAV, tid, historyColor);
    }
}

#endif