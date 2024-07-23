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
void mainCS(uint lane : SV_DispatchThreadID)
{
    if (lane >= pushConsts.uploadCount)
    {
        return;
    }
    
    /**
        uint4 indexingDataBuffer[];
        uint4 CollectUploadDataBin[];
    **/
    const uint stripe = 4 * 4;
    const uint4 indexingInfo = ByteAddressBindless(pushConsts.indexingBufferId).Load<uint4>(lane * stripe);

    RWByteAddressBuffer gpuSceneBuffer = RWByteAddressBindless(pushConsts.GPUSceneBufferId);
    ByteAddressBuffer collectedDataBuffer = ByteAddressBindless(pushConsts.collectedUploadDataBufferId);

    const uint scatterBase  = indexingInfo.x;
    const uint uint4Count   = indexingInfo.y;
    const uint bufferOffset = indexingInfo.z;

    // Fill data in GPU scene.
    for (uint i = 0; i < uint4Count; i ++)
    {
        const uint storePos = (bufferOffset + i) * stripe;
        const uint loadPos  = (scatterBase  + i) * stripe;
        gpuSceneBuffer.Store<uint4>(storePos, collectedDataBuffer.Load<uint4>(loadPos));
    }
}

#endif // !GPUSCENE_SCATTER_UPLOAD

#endif // HLSL only area end.