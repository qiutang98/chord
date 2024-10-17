// 2x downsample pass for GBuffer.
#include "base.h"

struct HalfDownsamplePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(HalfDownsamplePushConsts);

    uint depthTextureId;
    uint pixelNormalTextureId;
    uint aoRoughnessMetallicTextureId;
    uint motionVectorTextureId;

    uint halfPixelNormalRSId; 
    uint halfDeviceZId;
    uint halfMotionVectorId;
    uint halfRoughnessId;  

    uint2 workDim;
    float2 workTexelSize;

    uint pointClampedEdgeSampler;
    uint linearClampedEdgeSampler;

    uint vertexNormalTextureId;
    uint halfVertexNormalId;
};
CHORD_PUSHCONST(HalfDownsamplePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 
#include "base.hlsli"
#include "debug.hlsli"

// Half resolution gbuffer downsample, we use most closet pixel.  
[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);

    if (all(workPos < pushConsts.workDim))
    { 
        //
        float2 uv = (workPos + 0.5) * pushConsts.workTexelSize;

        Texture2D<float> depthTexture = TBindless(Texture2D, float, pushConsts.depthTextureId);
        Texture2D<float4> pixelNormalTexture = TBindless(Texture2D, float4, pushConsts.pixelNormalTextureId);
        Texture2D<float4> vertexNormalTexture = TBindless(Texture2D, float4, pushConsts.vertexNormalTextureId);
        Texture2D<float4> aoRoughnessMetallicTexture = TBindless(Texture2D, float4, pushConsts.aoRoughnessMetallicTextureId);
        Texture2D<float2> motionVectorTexture = TBindless(Texture2D, float2, pushConsts.motionVectorTextureId);

        //
        SamplerState pointClampSampler = Bindless(SamplerState, pushConsts.pointClampedEdgeSampler);
        SamplerState linearClampSampler = Bindless(SamplerState, pushConsts.linearClampedEdgeSampler);

        //
        float  deviceZ      = 0.0;
        float4 pixelNormal  = 0.0;
        float2 motionVector = 0.0;
        float4 vertexNormal  = 0.0;
        float roughness     = 1.0;

        //
        const float4 depthx4 = depthTexture.GatherRed(pointClampSampler, uv, kGatherOffset[0], kGatherOffset[1], kGatherOffset[2], kGatherOffset[3]);
        [unroll(4)]
        for (uint i = 0; i < 4; i ++)
        {
            [branch]
            if (deviceZ < depthx4[i])
            {
                deviceZ = depthx4[i];

                pixelNormal  = pixelNormalTexture.SampleLevel(pointClampSampler, uv, 0, kGatherOffset[i]);
                motionVector = motionVectorTexture.SampleLevel(pointClampSampler, uv, 0, kGatherOffset[i]);
                roughness    = aoRoughnessMetallicTexture.SampleLevel(pointClampSampler, uv, 0, kGatherOffset[i]).g;
                vertexNormal = vertexNormalTexture.SampleLevel(pointClampSampler, uv, 0, kGatherOffset[i]);
            }
        }

        storeRWTexture2D_float4(pushConsts.halfVertexNormalId,  workPos, vertexNormal);
        storeRWTexture2D_float4(pushConsts.halfPixelNormalRSId, workPos, pixelNormal);
        storeRWTexture2D_float2(pushConsts.halfMotionVectorId,  workPos, motionVector);
        storeRWTexture2D_float1(pushConsts.halfDeviceZId,       workPos, deviceZ);
        storeRWTexture2D_float1(pushConsts.halfRoughnessId,     workPos, roughness);
    }
}

#endif 