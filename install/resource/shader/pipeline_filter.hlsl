#include "gltf.h"

struct PipelineFilterPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(PipelineFilterPushConst);

    uint cameraViewId;
    uint drawedMeshletCountId;
    uint drawedMeshletCmdId;
    uint drawedMeshletCountId_1;
    uint drawedMeshletCmdId_1;
    uint targetAlphaMode;  // 0 opaque, 1 masked, 2 blend.
    uint targetTwoSide;    // 
};
CHORD_PUSHCONST(PipelineFilterPushConst, pushConsts);

#ifndef __cplusplus

#include "debug.hlsli"

#define FILTER_GROUP_BATCH 4
#define FILTER_CHECK(x) 

[numthreads(1, 1, 1)]
void filterPipelineParamCS()
{
    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);

    uint4 cmdParameter;
    cmdParameter.x = (meshletCount + 64 * FILTER_GROUP_BATCH - 1) / (64 * FILTER_GROUP_BATCH);
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;
    BATS(uint4, pushConsts.drawedMeshletCmdId, 0, cmdParameter);
}

// Loop all visibile meshlet, filter mesh and fill all draw cmd.
[numthreads(64, 1, 1)]
void fillPipelineDrawParamCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);

    bool  bVisible[FILTER_GROUP_BATCH];
    uint3  drawCmd[FILTER_GROUP_BATCH];

    for (uint i = 0; i < FILTER_GROUP_BATCH; i ++)
    {
        bVisible[i] = false;
    }

    GPUGLTFMeshlet meshlet;

    // 
    uint cacheObjectId  = ~0;
    uint cacheMeshletId = ~0;

    // Total visibile count.
    uint visibleCount = 0; 
    for(uint i = 0; i < FILTER_GROUP_BATCH; i ++)
    {
        const uint loadId = threadId * FILTER_GROUP_BATCH + i;

        // Break if reach edge.
        if (loadId >= meshletCount) 
        { 
            break; 
        }

        drawCmd[i] = BATL(uint3, pushConsts.drawedMeshletCmdId, loadId);
        uint objectId  = drawCmd[i].x;
        uint meshletId = drawCmd[i].y;

        //
        const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
        const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);
        const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
        const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
        const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

        const uint bTwoSided = materialInfo.bTwoSided;
        const uint alphaMode = materialInfo.alphaMode;

        FILTER_CHECK(bTwoSided == 0 || bTwoSided == 1);
        FILTER_CHECK(alphaMode == 0 || alphaMode == 1 || alphaMode == 2);

        // Update visibility.
        bVisible[i] = (pushConsts.targetAlphaMode == alphaMode) && (pushConsts.targetTwoSide == bTwoSided);
        if (bVisible[i])
        {
            visibleCount ++;
        }
    }

    const uint laneOffset       = WavePrefixSum(visibleCount);
    const uint visibleCountWave = WaveActiveSum(visibleCount);
    uint waveOffset;
    if (WaveIsFirstLane())
    {
        waveOffset = interlockedAddUint(pushConsts.drawedMeshletCountId_1, visibleCountWave);
    }
    waveOffset = WaveReadLaneFirst(waveOffset);

    // Visible, fill draw command.
    uint localStoreOffset = 0;
    for (uint i = 0; i < FILTER_GROUP_BATCH; i ++)
    {
        if (bVisible[i])
        {
            // Now get local draw cmd id.
            const uint drawCmdId = waveOffset + laneOffset + localStoreOffset;
            BATS(uint3, pushConsts.drawedMeshletCmdId_1, drawCmdId, drawCmd[i]);

            localStoreOffset ++;
        }
    }

    FILTER_CHECK(localStoreOffset == visibleCount);
}

#endif