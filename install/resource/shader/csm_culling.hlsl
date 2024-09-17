#include "base.h"

struct CSMCullingPushConsts
{
    uint cameraViewId;

};

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void cascadeInstanceCullingCS(uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;  

    bool bVisible = true;
    const bool bFirstThread = (localThreadIndex == 0);

    // Nanite instance culling work on all gltf object in scene. 
    if (workGroupId >= scene.GLTFObjectCount)
    {
        bVisible = false;
    }

    uint bvhNodeCount      = 0;
    uint meshletGroupCount = 0;
    uint storeBaseId       = 0;
    if (bVisible && bFirstThread)
    {
        const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, workGroupId);
        const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);

        const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
        const float4x4 localToClip = mul(perView.translatedWorldToClip, localToTranslatedWorld);

        const float3 posMin    = primitiveInfo.posMin;
        const float3 posMax    = primitiveInfo.posMax;
        const float3 posCenter = (posMin + posMax) * 0.5;
        const float3 extent    = posMax - posCenter;

        // Frustum visible culling: use obb.
        if (bVisible && shaderHasFlag(pushConsts.switchFlags, kFrustumCullingEnableBit))
        {
            if (frustumCulling(perView.frustumPlane, posCenter, extent, localToTranslatedWorld))
            {
                bVisible = false;
            }
        }

        if (bVisible)
        {
            const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
            const GPUBVHNode rootNode = BATL(GPUBVHNode, primitiveDataInfo.bvhNodeBuffer, primitiveInfo.bvhNodeOffset);

            // BVH node count sum.
            bvhNodeCount      += rootNode.bvhNodeCount;
            meshletGroupCount += primitiveInfo.meshletGroupCount;

            // Now get store base id.
            storeBaseId = interlockedAddUint(pushConsts.clusterGroupCountBuffer, meshletGroupCount);
        }
    }
}

[numthreads(64, 1, 1)]
void cascadeClusterGroupCullingCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint meshletGroupCount = BATL(uint, pushConsts.clusterGroupCountBuffer, 0);
    if (threadId >= meshletGroupCount)
    {
        return;
    }

    const uint2 clusterId = BATL(uint2, pushConsts.clusterGroupIdBuffer, threadId);
    const uint objectId       = clusterId.x;
    const uint clusterGroupId = clusterId.y;

    const GPUObjectGLTFPrimitive   objectInfo        = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer      primitiveInfo     = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFMaterialGPUData      materialInfo      = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData); 
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);

    ByteAddressBuffer meshletBuffer             = ByteAddressBindless(primitiveDataInfo.meshletBuffer);
    ByteAddressBuffer meshletGroupBuffer        = ByteAddressBindless(primitiveDataInfo.meshletGroupBuffer);
    ByteAddressBuffer meshletGroupIndicesBuffer = ByteAddressBindless(primitiveDataInfo.meshletGroupIndicesBuffer);

    const uint meshletGroupLoadIndex = primitiveInfo.meshletGroupOffset + clusterGroupId;
    const GPUGLTFMeshletGroup meshletGroup = meshletGroupBuffer.TypeLoad(GPUGLTFMeshletGroup, meshletGroupLoadIndex);

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    float4x4 localToView = mul(perView.translatedWorldToView, localToTranslatedWorld);
    float4x4 localToClip = mul(perView.translatedWorldToClip, localToTranslatedWorld);

    uint visibleMeshletCount = 0;
    uint visibleMeshletId[kClusterGroupMergeMaxCount];
    if (isMeshletGroupVisibile(perView.renderDimension.y, perView.cameraFovy, objectInfo, localToView, meshletGroup))
    {
        for (uint i = 0; i < meshletGroup.meshletCount; i ++)
        {
            const uint meshletIndicesLoadId = i + meshletGroup.meshletOffset + primitiveInfo.meshletGroupIndicesOffset;
            const uint meshletIndex = primitiveInfo.meshletOffset + meshletGroupIndicesBuffer.TypeLoad(uint, meshletIndicesLoadId);
            const GPUGLTFMeshlet meshlet = meshletBuffer.TypeLoad(GPUGLTFMeshlet, meshletIndex);

            if (isMeshletVisible(pushConsts.switchFlags, perView.frustumPlane, objectInfo, localToTranslatedWorld, meshlet, materialInfo))
            {
                visibleMeshletId[visibleMeshletCount] = meshletIndex;
                visibleMeshletCount ++;
            }
        }
    }
    check(visibleMeshletCount <= kClusterGroupMergeMaxCount);

    const uint visibleMeshletCountWaveSum = WaveActiveSum(visibleMeshletCount);
    uint storeBaseId;
    if (WaveIsFirstLane())
    {
        // 
        storeBaseId = interlockedAddUint(pushConsts.drawedMeshletCountId, visibleMeshletCountWaveSum);
    }
    storeBaseId = WaveReadLaneFirst(storeBaseId);

    const uint relativeOffset = WavePrefixSum(visibleMeshletCount);
    for (uint i = 0; i < visibleMeshletCount; i ++)
    {
        const uint storeId  = i + storeBaseId + relativeOffset;

        const uint3 drawCmd = uint3(objectId, visibleMeshletId[i], storeId);
        BATS(uint3, pushConsts.drawedMeshletCmdId, storeId, drawCmd);
    }
}

#endif

