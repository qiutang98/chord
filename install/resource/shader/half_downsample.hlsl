#ifndef SHADER_HALF_DOWNSAMPLE_HLSL
#define SHADER_HALF_DOWNSAMPLE_HLSL

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

    float2 srcTexelSize;
};
CHORD_PUSHCONST(HalfDownsamplePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 
#include "base.hlsli"
#include "debug.hlsli"

static const int2 kOffsetSample[4] = { int2(0, 0), int2(1, 0), int2(0, 1), int2(1, 1) }; 

// Half resolution gbuffer downsample, we use most closet pixel.  
[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);
    if (all(workPos < pushConsts.workDim))
    { 
        // Full resolution uv. 
        float2 uv = (workPos * 2 + 0.5) * pushConsts.srcTexelSize;

        Texture2D<float> depthTexture = TBindless(Texture2D, float, pushConsts.depthTextureId);
        Texture2D<float4> pixelNormalTexture = TBindless(Texture2D, float4, pushConsts.pixelNormalTextureId);
        Texture2D<float4> vertexNormalTexture = TBindless(Texture2D, float4, pushConsts.vertexNormalTextureId);
        Texture2D<float4> aoRoughnessMetallicTexture = TBindless(Texture2D, float4, pushConsts.aoRoughnessMetallicTextureId);
        Texture2D<float2> motionVectorTexture = TBindless(Texture2D, float2, pushConsts.motionVectorTextureId);

        //
        SamplerState pointClampSampler = Bindless(SamplerState, pushConsts.pointClampedEdgeSampler);
        SamplerState linearClampSampler = Bindless(SamplerState, pushConsts.linearClampedEdgeSampler);

        //
        int closestIndex;
        float closestDepth = 0.0;

        // Gather pattern position no sure for dxc-spirv translate :(
        // kGatherOffset[4] seems only valid in DX12. 
        [unroll(4)]
        for (uint i = 0; i < 4; i ++)
        {
            float2 sampleUv = uv + kOffsetSample[i] * pushConsts.srcTexelSize;
            float z = depthTexture.SampleLevel(pointClampSampler, sampleUv, 0);
            if (z > closestDepth)
            {
                closestDepth = z;
                closestIndex = i;
            }
        }

        // 
        float2 offsetUv = uv + kOffsetSample[closestIndex] * pushConsts.srcTexelSize;

        //
        float4 pixelNormal  =         pixelNormalTexture.SampleLevel(pointClampSampler, offsetUv, 0);
        float2 motionVector =        motionVectorTexture.SampleLevel(pointClampSampler, offsetUv, 0);
        float1  roughness   = aoRoughnessMetallicTexture.SampleLevel(pointClampSampler, offsetUv, 0).g;
        float4 vertexNormal =        vertexNormalTexture.SampleLevel(pointClampSampler, offsetUv, 0);

        // 
        storeRWTexture2D_float4(pushConsts.halfVertexNormalId,  workPos, vertexNormal);
        storeRWTexture2D_float4(pushConsts.halfPixelNormalRSId, workPos, pixelNormal);
        storeRWTexture2D_float2(pushConsts.halfMotionVectorId,  workPos, motionVector);
        storeRWTexture2D_float1(pushConsts.halfDeviceZId,       workPos, closestDepth);
        storeRWTexture2D_float1(pushConsts.halfRoughnessId,     workPos, roughness);
    }
}

#endif 

#endif // SHADER_HALF_DOWNSAMPLE_HLSL