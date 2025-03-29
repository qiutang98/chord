#ifndef SHADER_INSTANCE_CULLING_HLSL
#define SHADER_INSTANCE_CULLING_HLSL

#include "gltf.h"

// Instance culling do frustum culling in object level. 
// Then select suitable meshlet lod for nanite mesh and do some meshlet culling.

// NOTE: LOD level should keep same with main view.

struct InstanceCullingPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(InstanceCullingPushConst);

    uint cameraViewId;
    uint switchFlags;
    uint clusterGroupCountBuffer;
    uint clusterGroupIdBuffer;
    uint drawedMeshletCountId;
    uint drawedMeshletCmdId;

    uint instanceViewId;
    uint instanceViewOffset;
};
CHORD_PUSHCONST(InstanceCullingPushConst, pushConsts);


// Output: Uint3 Drawcalls
// .x is object id. 
// .y is meshlet id. 
// .z is current draw call id in result buffer. 

// .z is useful, because we will do hzb culling, pipeline filter and so on, the final draw just use .z to fetch drawcalls in current pass result buffer. 

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"
#include "debug_line.hlsli"
#include "nanite_shared.hlsli"

groupshared uint sharedIsVisible;
groupshared uint sharedStoreBaseId;
groupshared uint sharedMehsletGroupCount;

[numthreads(64, 1, 1)]
void instanceCullingCS(uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    const InstanceCullingViewInfo instanceView = BATL(InstanceCullingViewInfo, pushConsts.instanceViewId, pushConsts.instanceViewOffset);

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
        const float4x4 localToClip = mul(instanceView.translatedWorldToClip, localToTranslatedWorld);

        const float3 posMin    = primitiveInfo.posMin;
        const float3 posMax    = primitiveInfo.posMax;
        const float3 posCenter = (posMin + posMax) * 0.5;
        const float3 extent    = posMax - posCenter;

        // Frustum visible culling: use obb.
        if (bVisible && shaderHasFlag(pushConsts.switchFlags, kFrustumCullingEnableBit))
        {
            if (isOrthoProjection(localToClip))
            {
                bVisible = !orthoFrustumCulling(posCenter, extent, localToClip);
            } 
            else 
            {
                bVisible = !frustumCulling(instanceView.frustumPlanesRS, posCenter, extent, localToTranslatedWorld);
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

    if (bFirstThread)
    {
        sharedIsVisible         = bVisible;
        sharedStoreBaseId       = storeBaseId;
        sharedMehsletGroupCount = meshletGroupCount;
    }
    GroupMemoryBarrierWithGroupSync();
 
    meshletGroupCount = sharedMehsletGroupCount;
    storeBaseId       = sharedStoreBaseId;
    bVisible          = sharedIsVisible;

    if (bVisible)
    {
        for(uint i = localThreadIndex; i < meshletGroupCount; i += 64)
        {
            // Now get local draw cmd id.
            const uint objectOffset = i + storeBaseId;

            // Store cluster group id.
            const uint2 clusterGroupId = uint2(workGroupId, i);
            BATS(uint2, pushConsts.clusterGroupIdBuffer, objectOffset, clusterGroupId);
        }
    }
}

[numthreads(64, 1, 1)]
void clusterGroupCullingCS(uint threadId : SV_DispatchThreadID)
{
    const InstanceCullingViewInfo instanceView = BATL(InstanceCullingViewInfo, pushConsts.instanceViewId, pushConsts.instanceViewOffset);

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

    //
    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;

    // Use main view's factor to know what lod should we use.
    const float4x4 localToView = mul(perView.translatedWorldToView, localToTranslatedWorld);
    const float4x4 localToClip = mul(instanceView.translatedWorldToClip, localToTranslatedWorld);

    uint visibleMeshletCount = 0;
    uint visibleMeshletId[kClusterGroupMergeMaxCount];

    // Use main view's factor to know what lod should we use.
    if (isMeshletGroupVisibile(perView.renderDimension.y, perView.cameraFovy, objectInfo, localToView, meshletGroup))
    {
        for (uint i = 0; i < meshletGroup.meshletCount; i ++)
        {
            const uint meshletIndicesLoadId = i + meshletGroup.meshletOffset + primitiveInfo.meshletGroupIndicesOffset;
            const uint meshletIndex = primitiveInfo.meshletOffset + meshletGroupIndicesBuffer.TypeLoad(uint, meshletIndicesLoadId);
            const GPUGLTFMeshlet meshlet = meshletBuffer.TypeLoad(GPUGLTFMeshlet, meshletIndex);

            if (isMeshletVisible(pushConsts.switchFlags, instanceView.frustumPlanesRS, objectInfo, localToTranslatedWorld, localToClip, meshlet, materialInfo))
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

#endif // !__cplusplus

#endif // SHADER_INSTANCE_CULLING_HLSL