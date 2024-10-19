#include "base.h"

struct DisocclusionMaskPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(DisocclusionMaskPushConsts);

    uint2 workDim;
    float2 workTexelSize;


    uint cameraViewId;
    uint motionVectorId;
    uint depthTextureId;
    uint depthTextureId_LastFrame;

    uint normalRSId;
    uint normalRSId_LastFrame;

    uint pointClampedEdgeSampler;
    uint linearClampedEdgeSampler;
    uint disocclusionMaskUAV;
};
CHORD_PUSHCONST(DisocclusionMaskPushConsts, pushConsts);

#ifndef __cplusplus

#include "bindless.hlsli" 
#include "base.hlsli"
#include "debug.hlsli"

static const float kDisocclusionThreshold  = 0.9;

// https://gpuopen.com/manuals/fidelityfx_sdk/fidelityfx_sdk-page_techniques_denoiser/
// https://gpuopen.com/learn/getting-the-most-out-of-fidelityfx-brixelizer/
float disocclusionMaskFactor(float3 normal, float z, float3 positionRS, float3 normal_LastFrame, float3 positionRS_LastFrame)
{
    static const float normalDiffFalloffFactor   = 1.4;
    static const float distanceDiffFalloffFactor = 1.0;

    // if same, v is 0, otherwise is 1.0;
    float normalDiff = 1.0 - max(0.0, dot(normal, normal_LastFrame)); 

    // Use worldspace relative distance, and slightly scale by linear z.
    float distanceDiff = length(positionRS - positionRS_LastFrame) / z;

    // 
    float normalDiffFalloff   = exp(-normalDiffFalloffFactor   * normalDiff);
    float distanceDiffFallOff = exp(-distanceDiffFalloffFactor * distanceDiff);

    // When add normal factor, easy break geometry edge valid state. 
    // 
    return distanceDiffFallOff * normalDiffFalloff;
}

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);
    if (all(workPos < pushConsts.workDim))
    { 
        //
        float2 uv = (workPos + 0.5) * pushConsts.workTexelSize;

        // 
        const float2 motionVector = loadTexture2D_float2(pushConsts.motionVectorId, workPos);
        const float2 historyUv = motionVector + uv;
        const bool bHistoryOutOfBound = any(historyUv < 0.0) || any(historyUv > 1.0);

        // If history uv out of bounds, history unvalid.
        float disocclusionMask = float(bHistoryOutOfBound);

        if (!bHistoryOutOfBound)
        {
            float3 normalRS = loadTexture2D_float4(pushConsts.normalRSId, workPos).xyz * 2.0 - 1.0;
            normalRS = quantize(normalRS, 100.0);
            normalRS = normalize(normalRS);

            const float1 deviceZ = loadTexture2D_float1(pushConsts.depthTextureId, workPos);

            // Current frame position RS.
            const float3 positionRS = getPositionRS(uv, max(deviceZ, kFloatEpsilon), perView); 

            //
            SamplerState pointClampSampler = Bindless(SamplerState, pushConsts.pointClampedEdgeSampler);
            SamplerState linearClampSampler = Bindless(SamplerState, pushConsts.linearClampedEdgeSampler);


            float3 normalRS_LastFrame = sampleTexture2D_float4(pushConsts.normalRSId_LastFrame, historyUv, pointClampSampler).xyz * 2.0 - 1.0;
            normalRS_LastFrame = quantize(normalRS_LastFrame, 100.0);
            normalRS_LastFrame = normalize(normalRS_LastFrame);

            // Normal don't care relative world space change. 
            float3 positionRS_LastFrame;
            {
                const float1 deviceZ_LastFrame = sampleTexture2D_float1(pushConsts.depthTextureId_LastFrame, historyUv, pointClampSampler);
                positionRS_LastFrame = getPositionRS_LastFrame(historyUv, max(deviceZ_LastFrame, kFloatEpsilon), perView); 
                positionRS_LastFrame += float3(asDouble3(perView.cameraWorldPosLastFrame) - asDouble3(perView.cameraWorldPos));
            }

            const float linearZ = perView.zNear / deviceZ;
    
            const float f = disocclusionMaskFactor(normalRS, linearZ, positionRS, normalRS_LastFrame, positionRS_LastFrame);
            disocclusionMask = float(f < kDisocclusionThreshold);
        }

        storeRWTexture2D_float1(pushConsts.disocclusionMaskUAV, workPos, disocclusionMask);
    }
}

#endif 