#include "gltf.h"

#define kFrustumCullingEnableBit  0
#define kHZBCullingEnableBit      1
#define kMeshletConeCullEnableBit 2

// 4 meshlet per cluster group.
#define kClusterGroupMergeMaxCount 4

struct InstanceCullingPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(InstanceCullingPushConst);

    uint cameraViewId;
    uint switchFlags;
    uint clusterGroupCountBuffer;
    uint clusterGroupIdBuffer;

    uint hzb; // hzb texture id.  
    uint hzbMipCount;
    float uv2HzbX;
    float uv2HzbY;
    uint hzbMip0Width;
    uint hzbMip0Height;

    uint meshletCullCmdId;
    uint consumeVisibleObjectCount;
    uint drawedMeshletCountId;
    uint drawedMeshletCmdId;
    uint drawedMeshletCountId_1;
    uint drawedMeshletCmdId_1;
    uint drawedMeshletCountId_2;
    uint drawedMeshletCmdId_2;

    uint targetAlphaMode; // 0 opaque, 1 masked, 2 blend.
    uint targetTwoSide; // 
};
CHORD_PUSHCONST(InstanceCullingPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"
#include "debug_line.hlsli"

groupshared uint sharedIsVisible;
groupshared uint sharedStoreBaseId;
groupshared uint sharedMehsletGroupCount;

[numthreads(64, 1, 1)]
void instanceCullingCS(uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
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

// One pixel threshold.
#define kErrorPixelThreshold 1.0f
#define kErrorRadiusRoot     3e38f

// Meshlet group. 
bool isMeshletGroupVisibile(
    in const PerframeCameraView perView, 
    in const GPUObjectGLTFPrimitive objectInfo,
    in const float4x4 localToTranslatedWorld,
    in const float4x4 localToView,
    in const float4x4 localToClip,
    in const GPUGLTFMeshletGroup meshletGroup)
{
    const bool bFinalLod = (meshletGroup.parentError > kErrorRadiusRoot);
    const bool bFirstlOD = (meshletGroup.error < -0.5f);

    if (!bFinalLod)
    {
        float4 sphere = transformSphere(float4(meshletGroup.parentPosCenter, meshletGroup.parentError), localToView, objectInfo.basicData.scaleExtractFromMatrix.w);
        float parentError = projectSphereToScreen(sphere, perView.renderDimension.y, perView.camMiscInfo.x);
        if (parentError > 0.0f && parentError <= kErrorPixelThreshold) 
        { 
            // When eye in sphere, always > kErrorPixelThreshold, so visible.
            return false; 
        }
    }

    if (!bFirstlOD)
    {
        float4 sphere = transformSphere(float4(meshletGroup.clusterPosCenter, meshletGroup.error), localToView, objectInfo.basicData.scaleExtractFromMatrix.w);
        float error = projectSphereToScreen(sphere, perView.renderDimension.y, perView.camMiscInfo.x);
        if (error < 0.0f || error > kErrorPixelThreshold) 
        { 
            // When eye in sphere, always > kErrorPixelThreshold, meaning unvisible.
            return false;
        }
    }

    return true;
}

bool isMeshletVisible(
    in const PerframeCameraView perView, 
    in const GPUObjectGLTFPrimitive objectInfo,
    in const float4x4 localToTranslatedWorld,
    in const float4x4 localToView,
    in const float4x4 localToClip,
    in const GPUGLTFMeshlet meshlet,
    in const GLTFMaterialGPUData materialInfo)
{
    // Do cone culling before frustum culling, it compute faster.
    if ((materialInfo.bTwoSided == 0) && shaderHasFlag(pushConsts.switchFlags, kMeshletConeCullEnableBit))
    {
    	float3 cameraPosLS = mul(objectInfo.basicData.translatedWorldToLocal, float4(0, 0, 0, 1)).xyz;
        if (dot(normalize(meshlet.coneApex - cameraPosLS), meshlet.coneAxis) >= meshlet.coneCutOff)
        {
            return false;
        }
    }

    const float3 posMin    = meshlet.posMin;
    const float3 posMax    = meshlet.posMax;
    const float3 posCenter = 0.5 * (posMin + posMax);
    const float3 extent    = posMax - posCenter;

    // Frustum visible culling: use obb.
    if (shaderHasFlag(pushConsts.switchFlags, kFrustumCullingEnableBit))
    {
        if (frustumCulling(perView.frustumPlane, posCenter, extent, localToTranslatedWorld))
        {
            return false;
        }
    }

    return true;
}

[numthreads(64, 1, 1)]
void clusterGroupCullingCS(uint threadId : SV_DispatchThreadID)
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
    if (isMeshletGroupVisibile(perView, objectInfo, localToTranslatedWorld, localToView, localToClip, meshletGroup))
    {
        for (uint i = 0; i < meshletGroup.meshletCount; i ++)
        {
            const uint meshletIndicesLoadId = i + meshletGroup.meshletOffset + primitiveInfo.meshletGroupIndicesOffset;
            const uint meshletIndex = primitiveInfo.meshletOffset + meshletGroupIndicesBuffer.TypeLoad(uint, meshletIndicesLoadId);
            const GPUGLTFMeshlet meshlet = meshletBuffer.TypeLoad(GPUGLTFMeshlet, meshletIndex);

            if (isMeshletVisible(perView, objectInfo, localToTranslatedWorld, localToView, localToClip, meshlet, materialInfo))
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

[numthreads(1, 1, 1)]
void fillCullingParamCS()
{
    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);

    uint4 cmdParameter;
    cmdParameter.x = (meshletCount + 63) / 64;
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;
    BATS(uint4, pushConsts.meshletCullCmdId, 0, cmdParameter);
}

[numthreads(64, 1, 1)]
void HZBCullingCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);
    if (threadId >= meshletCount)
    {
        return;
    }


    const uint3 drawCmd = BATL(uint3, pushConsts.drawedMeshletCmdId, threadId);

    uint objectId  = drawCmd.x;
    uint meshletId = drawCmd.y;

#if DIM_HZB_CULLING_PHASE_0
    check(drawCmd.z == threadId);
#endif 

    //
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData); 
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    const float3 posMin = meshlet.posMin;
    const float3 posMax = meshlet.posMax;
    const float3 posCenter = (posMin + posMax) * 0.5;
    const float3 extent = posMax - posCenter;

    bool bVisible = true;
    if (shaderHasFlag(pushConsts.switchFlags, kHZBCullingEnableBit))
    {
        float3 maxUVz = -10.0;
        float3 minUVz =  10.0;

        const float4x4 mvp = 
        #if DIM_HZB_CULLING_PHASE_0
            // Project to prev frame and do some occlusion culling. 
            mul(perView.translatedWorldToClipLastFrame, objectInfo.basicData.localToTranslatedWorldLastFrame);
        #else
            mul(perView.translatedWorldToClip, localToTranslatedWorld);
        #endif

        for (uint i = 0; i < 8; i ++)
        {
            const float3 extentPos = posCenter + extent * kExtentApplyFactor[i];
            const float3 UVz = projectPosToUVz(extentPos, mvp);
            minUVz = min(minUVz, UVz);
            maxUVz = max(maxUVz, UVz);
        }

        if (maxUVz.z < 1.0f && minUVz.z > 0.0f) // No cross near or far plane.
        {
            // Clamp min & max uv.
            minUVz.xy = saturate(minUVz.xy);
            maxUVz.xy = saturate(maxUVz.xy);

            // UV convert to hzb space.
            // NOTE: It cause float error here, so we must do 4x4 sample hzb later. :(
            maxUVz.x *= pushConsts.uv2HzbX;
            maxUVz.y *= pushConsts.uv2HzbY;
            minUVz.x *= pushConsts.uv2HzbX;
            minUVz.y *= pushConsts.uv2HzbY;

            const float2 bounds = (maxUVz.xy - minUVz.xy) * float2(pushConsts.hzbMip0Width, pushConsts.hzbMip0Height);
            if (any(bounds <= 0))
            {
                // Zero area just culled.
                bVisible = false;
            }
            else
            {
                // Use max bounds dim as sample mip level.
                const uint mipLevel = uint(min(ceil(log2(max(1.0, max2(bounds)))), pushConsts.hzbMipCount - 1));
                const uint2 mipSize = uint2(pushConsts.hzbMip0Width, pushConsts.hzbMip0Height) / uint(exp2(mipLevel));

                // Load hzb texture.
                Texture2D<float> hzbTexture = TBindless(Texture2D, float, pushConsts.hzb);

                const int2 minPos = uint2(minUVz.xy * mipSize);
                const int2 maxPos = uint2(maxUVz.xy * mipSize);

                    float zMin = hzbTexture.Load(int3(minPos.x, maxPos.y, mipLevel));
                zMin = min(zMin, hzbTexture.Load(int3(maxPos.x, minPos.y, mipLevel)));
                zMin = min(zMin, hzbTexture.Load(int3(maxPos, mipLevel)));
                zMin = min(zMin, hzbTexture.Load(int3(minPos, mipLevel)));

                #if 1 // Avoid float uv precision cause sample error.
                {
                    zMin = min(zMin, hzbTexture.Load(int3(minPos + int2(-1, -1), mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(minPos + int2(-1,  0), mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(minPos + int2( 0, -1), mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(maxPos + int2( 1,  1), mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(maxPos + int2( 1,  0), mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(maxPos + int2( 0,  1), mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(maxPos.x + 1, minPos.y + 0, mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(maxPos.x + 0, minPos.y - 1, mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(maxPos.x + 1, minPos.y - 1, mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(minPos.x - 1, maxPos.y + 1, mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(minPos.x - 1, maxPos.y + 0, mipLevel)));
                    zMin = min(zMin, hzbTexture.Load(int3(minPos.x + 0, maxPos.y + 1, mipLevel)));
                }
                #endif

                if (zMin > maxUVz.z)
                {
                    // Occluded by hzb.
                    bVisible = false;
                }
            }
        }
    }

    const uint visibleCount    = WaveActiveCountBits(bVisible);
    const uint visibleOffsetId = WavePrefixCountBits(bVisible);

#if DIM_HZB_CULLING_PHASE_0
    const uint unVisibleCount    = WaveActiveCountBits(!bVisible);
    const uint unVisibleOffsetId = WavePrefixCountBits(!bVisible);
#endif

    uint visibleStoreBaseId;
    uint unvisibleStoreBaseId;
    if (WaveIsFirstLane())
    {
        visibleStoreBaseId   = interlockedAddUint(pushConsts.drawedMeshletCountId_1, visibleCount);
#if DIM_HZB_CULLING_PHASE_0
        unvisibleStoreBaseId = interlockedAddUint(pushConsts.drawedMeshletCountId_2, unVisibleCount);
#endif
    }

    visibleStoreBaseId   = WaveReadLaneFirst(visibleStoreBaseId);
#if DIM_HZB_CULLING_PHASE_0
    unvisibleStoreBaseId = WaveReadLaneFirst(unvisibleStoreBaseId);
#endif

    // If visible, add to draw list.
    if (bVisible)
    {
        uint drawCmdId = visibleStoreBaseId + visibleOffsetId;
        BATS(uint3, pushConsts.drawedMeshletCmdId_1, drawCmdId, drawCmd);

    #if DIM_PRINT_DEBUG_BOX
        uint packColor = simpleHashColorPack(meshletId);
        LineDrawVertex worldPosExtents[8];
        for (uint i = 0; i < 8; i ++)
        {
            const float3 extentPos = posCenter + extent * kExtentApplyFactor[i];
            const float3 extentPosRS = mul(localToTranslatedWorld, float4(extentPos, 1.0)).xyz;
            worldPosExtents[i].translatedWorldPos = extentPosRS;
            worldPosExtents[i].color = packColor;
        }
        addBox(scene, worldPosExtents);
    #endif
    }
    #if DIM_HZB_CULLING_PHASE_0
    else
    {
        uint drawCmdId = unvisibleStoreBaseId + unVisibleOffsetId;
        BATS(uint3, pushConsts.drawedMeshletCmdId_2, drawCmdId, drawCmd);
    }
    #endif
}

// Loop all visibile meshlet, filter mesh and fill all draw cmd.
[numthreads(64, 1, 1)]
void fillPipelineDrawParamCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    bool bVisible = true;
    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);
    if (threadId >= meshletCount)
    {
        bVisible = false;
    }

    uint3 drawCmd;
    GPUGLTFMeshlet meshlet;
    if (bVisible)
    {
        drawCmd = BATL(uint3, pushConsts.drawedMeshletCmdId, threadId);
        uint objectId  = drawCmd.x;
        uint meshletId = drawCmd.y;

        //
        const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
        const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);
        const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
        const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);

        // Load meshlet.
        meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

        const uint bTwoSided = materialInfo.bTwoSided;
        const uint alphaMode = materialInfo.alphaMode;
        check(bTwoSided == 0 || bTwoSided == 1);
        check(alphaMode == 0 || alphaMode == 1 || alphaMode == 2);

        // Update visibility.
        bVisible = (pushConsts.targetAlphaMode == alphaMode) && (pushConsts.targetTwoSide == bTwoSided);
    }

    const uint localIndex   = WavePrefixCountBits(bVisible);
    const uint visibleCount = WaveActiveCountBits(bVisible);
    uint storeBaseId;
    if (WaveIsFirstLane())
    {
        storeBaseId = interlockedAddUint(pushConsts.drawedMeshletCountId_1, visibleCount);
    }
    storeBaseId = WaveReadLaneFirst(storeBaseId);

    // Visible, fill draw command.
    if (bVisible)
    {
        // Now get local draw cmd id.
        const uint drawCmdId = storeBaseId + localIndex;
        BATS(uint3, pushConsts.drawedMeshletCmdId_1, drawCmdId, drawCmd);
    }
}

[numthreads(1, 1, 1)]
void fillMeshShadingParamCS()
{
    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);

    uint4 cmdParameter;
    cmdParameter.x = meshletCount;
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;
    BATS(uint4, pushConsts.meshletCullCmdId, 0, cmdParameter);
}

#endif // !__cplusplus