#ifndef SHADER_GPUSCENE_HLSL
#define SHADER_GPUSCENE_HLSL

// GPU scene shader, sparse upload and management. 

#include "base.h"
#include "gltf.h"

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
#include "debug.hlsli"

// This shader scatter upload new info in GPU primitive buffer. 
// 
#ifdef GPUSCENE_SCATTER_UPLOAD
 
// Push const type. 
CHORD_PUSHCONST(GPUSceneScatterUploadPushConsts, pushConsts);  

// Scatter upload shader.
[numthreads(kGPUSceneScatterUploadDimX, 1, 1)] 
void mainCS(uint threadId : SV_DispatchThreadID)
{
    if (threadId >= pushConsts.uploadCount)
    {
        return;
    } 
    
    /**
        uint4 indexingDataBuffer[];
        uint4 CollectUploadDataBin[];
    **/
    const uint4 indexingInfo = BATL(uint4, pushConsts.indexingBufferId, threadId);

    RWByteAddressBuffer gpuSceneBuffer = RWByteAddressBindless(pushConsts.GPUSceneBufferId);
    ByteAddressBuffer collectedDataBuffer = ByteAddressBindless(pushConsts.collectedUploadDataBufferId); 

    const uint scatterBase  = indexingInfo.x;
    const uint uint4Count   = indexingInfo.y; 
    const uint bufferOffset = indexingInfo.z;

    // Fill data in GPU scene.
    for (uint i = 0; i < uint4Count; i ++)
    {
        const uint loadPosition = scatterBase + i;
        const uint storePosition = bufferOffset + i;
        
        const uint4 loadData = collectedDataBuffer.TypeLoad(uint4, loadPosition);
        gpuSceneBuffer.TypeStore(uint4, storePosition, loadData);
    }
}

#endif // !GPUSCENE_SCATTER_UPLOAD

#endif // HLSL only area end.

#endif // SHADER_GPUSCENE_HLSL