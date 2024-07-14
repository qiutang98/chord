// GPU scene shader, sparse upload and management. 

#include "base.h"

// Scatter upload push const. 
// 
struct GPUSceneScatterUploadPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(GPUSceneScatterUploadPushConsts);

    uint indexingBufferId;
    uint collectedUploadDataBufferId;
    uint uploadCount;
    uint GPUSceneBufferId;
};

#define kGPUSceneScatterUploadDimX 128

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 

// This shader scatter upload new info in GPU primitive buffer. 
// 
#ifdef GPUSCENE_SCATTER_UPLOAD

// Push const type. 
CHORD_PUSHCONST(GPUSceneScatterUploadPushConsts, pushConsts);

// Scatter upload shader.
[numthreads(kGPUSceneScatterUploadDimX, 1, 1)]
void mainCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint lane = dispatchThreadId.x;
    if (lane >= pushConsts.uploadCount)
    {
        return;
    }
    
    /**
        uvec4 indexingDataBuffer[];
        float4 CollectUploadDataBin[];
    **/
    StructuredBuffer<uint4> indexingBuffer = TBindless(StructuredBuffer, uint4, pushConsts.indexingBufferId);
    const uint4 indexingInfo = indexingBuffer[lane];

    RWStructuredBuffer<float4> gpuSceneBuffer = TBindless(RWStructuredBuffer, float4, pushConsts.GPUSceneBufferId);
    StructuredBuffer<float4> collectedDataBuffer = TBindless(StructuredBuffer, float4, pushConsts.collectedUploadDataBufferId);

    const uint scatterBase  = indexingInfo.x;
    const uint float4Count  = indexingInfo.y;
    const uint bufferOffset = indexingInfo.z;

    // Fill data in GPU scene.
    for(uint i = 0; i < float4Count; i ++)
    {
        gpuSceneBuffer[bufferOffset + i] = collectedDataBuffer[scatterBase + i];
    }
}

#endif // !GPUSCENE_SCATTER_UPLOAD

#endif // HLSL only area end.