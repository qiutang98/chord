#ifndef SHADER_GI_HISTORY_REPROJECTION_HLSL
#define SHADER_GI_HISTORY_REPROJECTION_HLSL

#include "gi.h"

struct GIHistoryReprojectPushConsts
{
    uint2 gbufferDim;

    uint cameraViewId;
    uint motionVectorId;

    uint reprojectGIUAV;
    uint depthSRV;
    uint disoccludedMaskSRV;
    uint historyGISRV;

    uint reprojectSpecularUAV;
    uint historySpecularSRV;

    uint specularStatUAV;
    uint specularIntersectSRV;

    uint normalRSId;
    uint normalRSId_LastFrame;
    uint depthTextureId_LastFrame;

};
CHORD_PUSHCONST(GIHistoryReprojectPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

static const float kDisocclusionThreshold  = 0.9;
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
    return distanceDiffFallOff * normalDiffFalloff;
}


groupshared float4 sRadiance[64];

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const uint2 gid = remap8x8(localThreadIndex); 
    const uint2 tid = workGroupId * 8 + gid;

    sRadiance[localThreadIndex] = 0.0;
    GroupMemoryBarrierWithGroupSync();

    if (any(tid >= pushConsts.gbufferDim))
    {
        // Out of bound pre-return.  
        return; 
    }

    float4 reprojectGI = 0.0;
    float4 reprojectSpecular = 0.0;

    float deviceZ = loadTexture2D_float1(pushConsts.depthSRV, tid);
    bool bSuccess = false;
    if (deviceZ > 0.0)
    {
        float2 pixel_uv = (tid + 0.5) / pushConsts.gbufferDim;
        float2 pixel_history_uv = pixel_uv + loadTexture2D_float2(pushConsts.motionVectorId, tid);
        const float3 positionRS = getPositionRS(pixel_uv, max(deviceZ, kFloatEpsilon), perView); 

        bool bCanReproject = loadTexture2D_float1(pushConsts.disoccludedMaskSRV, tid) < 1e-3f;

        // 
        if (bCanReproject)
        {
            if (all(pixel_history_uv >= 0.0) && all(pixel_history_uv <= 1.0))
            {
                reprojectGI = sampleTexture2D_float4(pushConsts.historyGISRV, pixel_history_uv, getPointClampEdgeSampler(perView));
            }
        }

        float4 specularTraceResult = loadTexture2D_float4(pushConsts.specularIntersectSRV, tid);
        if (specularTraceResult.w > 1e-3f)
        {

            const float rayLenght = length(positionRS);

            const float3 rayDir = positionRS / rayLenght;
            const float3 rayHit = rayDir * (rayLenght + specularTraceResult.w);

            float3 hitPositioinRS_LastFrame = rayHit + float3(perView.cameraWorldPos.getDouble3() - perView.cameraWorldPosLastFrame.getDouble3());
            float3 hitUVz_LastFrame = projectPosToUVz(hitPositioinRS_LastFrame, perView.translatedWorldToClipLastFrame);
            if (all(hitUVz_LastFrame > 0.0) && all(hitUVz_LastFrame < 1.0))
            {
                pixel_history_uv = hitUVz_LastFrame.xy;
            }
        }

        if (bCanReproject)
        {
            if (all(pixel_history_uv >= 0.0) && all(pixel_history_uv <= 1.0))
            {
                float3 normalRS = loadTexture2D_float4(pushConsts.normalRSId, tid).xyz * 2.0 - 1.0;
                normalRS = normalize(normalRS);

                float3 normalRS_LastFrame = sampleTexture2D_float4(pushConsts.normalRSId_LastFrame, pixel_history_uv,  getPointClampEdgeSampler(perView)).xyz * 2.0 - 1.0;
                normalRS_LastFrame = normalize(normalRS_LastFrame);

                float3 positionRS_LastFrame;
                {
                    const float1 deviceZ_LastFrame = sampleTexture2D_float1(pushConsts.depthTextureId_LastFrame, pixel_history_uv,  getPointClampEdgeSampler(perView));
                    positionRS_LastFrame = getPositionRS_LastFrame(pixel_history_uv, max(deviceZ_LastFrame, kFloatEpsilon), perView); 
                    positionRS_LastFrame += float3(perView.cameraWorldPosLastFrame.getDouble3() - perView.cameraWorldPos.getDouble3());
                }
                const float linearZ = perView.zNear / deviceZ;

                const float f = disocclusionMaskFactor(normalRS, linearZ, positionRS, normalRS_LastFrame, positionRS_LastFrame);
                if (f > kDisocclusionThreshold)
                {
                    bSuccess = true;
                    reprojectSpecular = sampleTexture2D_float4(pushConsts.historySpecularSRV, pixel_history_uv, getPointClampEdgeSampler(perView));
                }
            }
        }
    }

    storeRWTexture2D_float4(pushConsts.reprojectGIUAV, tid, reprojectGI);
    storeRWTexture2D_float4(pushConsts.reprojectSpecularUAV, tid, reprojectSpecular);

    if (any(workGroupId >= pushConsts.gbufferDim / 8))
    {
        return;
    }

    sRadiance[localThreadIndex] = float4(reprojectSpecular.xyz, bSuccess ? 1.0 : 0.0);
    GroupMemoryBarrierWithGroupSync();

    if (localThreadIndex < 32)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 32];
    }
    GroupMemoryBarrierWithGroupSync();
    if (localThreadIndex < 16)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 16];
    }
    if (localThreadIndex < 8)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 8];
    }
    if (localThreadIndex < 4)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 4];
    }
    if (localThreadIndex < 2)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 2];
    }
    if (localThreadIndex < 1)
    {
        float4 radianceSum = sRadiance[0] + sRadiance[1];
        float3 avgRadiance = radianceSum.w > 0.0 ? radianceSum.xyz / radianceSum.w : 0.0;

        if (pushConsts.specularStatUAV != kUnvalidIdUint32)
        {
            storeRWTexture2D_float3(pushConsts.specularStatUAV, workGroupId, avgRadiance);
        }
    }
}

#endif 

#endif // SHADER_GI_HISTORY_REPROJECTION_HLSL