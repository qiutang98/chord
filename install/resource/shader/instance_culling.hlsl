#include "gltf.h"

#define kFrustumCullingEnableBit  0
#define kHZBCullingEnableBit      1
#define kMeshletConeCullEnableBit 2

struct InstanceCullingPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(InstanceCullingPushConst);

    uint cameraViewId;
    uint switchFlags;
    uint bvhNodeCountBuffer;
    uint bvhNodeIdBuffer;

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

#define kBVHNodeProduceCountPos    0 
#define kBVHNodeConsumeCountPos    1
#define kBVHNodeCheckNodeCountPos  2
#define kBVHNodeMaxNodeCountPos    3

[numthreads(64, 1, 1)]
void instanceCullingCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;  

    bool bVisible = true;

    // Nanite instance culling work on all gltf object in scene. 
    if (threadId >= scene.GLTFObjectCount)
    {
        bVisible = false;
    } 

    uint bvhNodeCount = 0;
    if (bVisible)
    {
        const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, threadId);
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
            bvhNodeCount += rootNode.bvhNodeCount;
        }
    }

    const uint totalBVHNodeCount = WaveActiveSum(bvhNodeCount);
    RWByteAddressBuffer countBufferAcess = RWByteAddressBindless(pushConsts.bvhNodeCountBuffer);

    const uint localIndex   = WavePrefixCountBits(bVisible);
    const uint visibleCount = WaveActiveCountBits(bVisible);
    uint storeBaseId;
    if (WaveIsFirstLane())
    {
        // Buffer[1] Total node count increment.
        uint totalCountStoreBseId;
        countBufferAcess.InterlockedAdd(kBVHNodeCheckNodeCountPos * 4, totalBVHNodeCount, totalCountStoreBseId);

        // Buffer[0] is node count.
        countBufferAcess.InterlockedAdd(kBVHNodeProduceCountPos * 4, visibleCount, storeBaseId);
    }
    storeBaseId = WaveReadLaneFirst(storeBaseId);

    if (bVisible)
    {
        // Now get local draw cmd id.
        const uint objectOffset = storeBaseId + localIndex;

        // Store bvh root node.
        const uint2 bvhRootNodeId = uint2(threadId, 0);
        BATS(uint2, pushConsts.bvhNodeIdBuffer, objectOffset, bvhRootNodeId);
    }
}

[numthreads(1, 1, 1)]
void prepareBVHTraverseCS()
{
    const uint kMaxNodeCount  = BATL(uint, pushConsts.bvhNodeCountBuffer, kBVHNodeCheckNodeCountPos);
    const uint kRootNodeCount = BATL(uint, pushConsts.bvhNodeCountBuffer, kBVHNodeProduceCountPos);

    // Store max 
    BATS(uint, pushConsts.bvhNodeCountBuffer, kBVHNodeMaxNodeCountPos, kMaxNodeCount);

    // Dispatch traverse count. 
    uint4 dispatchParam;
    dispatchParam.x = kRootNodeCount > 0 ? 1024 : 0; // 1024 warp. wave32
    dispatchParam.y = 1;
    dispatchParam.z = 1;
    dispatchParam.w = 1;
    BATS(uint4, pushConsts.meshletCullCmdId, 0, dispatchParam);
}

[[vk::binding(0, 1)]] globallycoherent RWStructuredBuffer<uint2> rwBVHNodeIdBuffer;
[[vk::binding(1, 1)]] globallycoherent RWStructuredBuffer<int>  rwCounterBuffer;

groupshared uint sharedNodeCount;
groupshared uint sharedNodeId[kWaveSize];

// One pixel threshold.
#define kErrorPixelThreshold 1.0f
#define kErrorRadiusRoot     3e38f

bool isNodeVisible(
    in const PerframeCameraView perView, 
    in const GPUObjectGLTFPrimitive objectInfo,
    in const float4x4 localToView,
    float4 sphere)
{
    float4 viewSphere = transformSphere(sphere, localToView, objectInfo.basicData.scaleExtractFromMatrix.w);

    // Project sphere to screen get error.
    float parentError = projectSphereToScreen(viewSphere, perView.renderDimension.y, perView.camMiscInfo.x);

    // Eye in the sphere, so always visible.
    if (parentError < 0.0f)
    {
        return true;
    }

    // Only parent error max than threshold can render.
    return parentError > kErrorPixelThreshold;
}

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


#if kNaniteBVHLevelNodeCount != 8
    #error "Expect bvh node count per node is 8."
#endif

[numthreads(kWaveSize, 1, 1)]
void bvhTraverseCS(uint localThreadIndex : SV_GroupIndex)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;  

    // Get max node count.
    const uint kMaxNodeCount = rwCounterBuffer[kBVHNodeMaxNodeCountPos];

    // Only for first lane.
    uint consumeBVHNodeId = kUnvalidIdUint32; 
    uint bvhNodeCountNeedCheck;

    // 
    uint objectId;
    uint nodeId   = kUnvalidIdUint32;

    sharedNodeId[localThreadIndex] = kUnvalidIdUint32;
    if (WaveIsFirstLane())
    {
        sharedNodeCount = 0;
    }

    // 
    uint cacheObjectId = kUnvalidIdUint32;

    // Load object always traverse meshlet and root node id. 
    GPUObjectGLTFPrimitive   objectInfo;
    GLTFPrimitiveBuffer      primitiveInfo;
    GLTFPrimitiveDatasBuffer primitiveDataInfo;
    GLTFMaterialGPUData      materialInfo;

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    float4x4 localToView = mul(perView.translatedWorldToView, localToTranslatedWorld);
    float4x4 localToClip = mul(perView.translatedWorldToClip, localToTranslatedWorld);

    // Persistent thread body.
    while (true)
    {
        if (WaveIsFirstLane())
        {
            InterlockedMax(rwCounterBuffer[kBVHNodeCheckNodeCountPos], 0, bvhNodeCountNeedCheck);
        }
        bvhNodeCountNeedCheck = WaveReadLaneFirst(bvhNodeCountNeedCheck);

        // Reset object id if all node unvalid.
        if (sharedNodeCount == 0)
        {
            objectId = kUnvalidIdUint32;
        }

        // When check id is zero, all node checked.
        bool bAllBVHNodeChecked = (bvhNodeCountNeedCheck == 0);
        if (bAllBVHNodeChecked)
        {
            break;
        }

        // Object no ready, load new one.
        if (WaveIsFirstLane() && !bAllBVHNodeChecked && objectId == kUnvalidIdUint32)
        {
            check(sharedNodeCount == 0);

            // Allocate a consume node id.
            if (consumeBVHNodeId == kUnvalidIdUint32)
            {
                InterlockedAdd(rwCounterBuffer[kBVHNodeConsumeCountPos], 1, consumeBVHNodeId);
            }

            // 
            if (consumeBVHNodeId < kMaxNodeCount)
            {
                uint2 cmd = kUnvalidIdUint32;
                InterlockedExchange(rwBVHNodeIdBuffer[consumeBVHNodeId].x, kUnvalidIdUint32, cmd.x);
                InterlockedExchange(rwBVHNodeIdBuffer[consumeBVHNodeId].y, kUnvalidIdUint32, cmd.y);

                if (any(cmd != kUnvalidIdUint32))
                {
                    // Spinning until all ready.
                    while (cmd.x == kUnvalidIdUint32) { InterlockedExchange(rwBVHNodeIdBuffer[consumeBVHNodeId].x, kUnvalidIdUint32, cmd.x); }
                    while (cmd.y == kUnvalidIdUint32) { InterlockedExchange(rwBVHNodeIdBuffer[consumeBVHNodeId].y, kUnvalidIdUint32, cmd.y); }

                    objectId = cmd.x;

                    // Add node.
                    sharedNodeId[sharedNodeCount] = cmd.y;
                    sharedNodeCount ++;

                    // Current already used id, reset for next allocate.
                    consumeBVHNodeId  = kUnvalidIdUint32;
                }
            }
        }

        // Get object id and node id.
        objectId = WaveReadLaneFirst(objectId);
        nodeId   = (localThreadIndex < sharedNodeCount) ? sharedNodeId[localThreadIndex] : kUnvalidIdUint32;

        // Reset shared node count. 
        if (WaveIsFirstLane())
        {
            sharedNodeCount = 0;
        }

        // Load object info.
        if (cacheObjectId != objectId)
        {
            cacheObjectId = objectId;

            // Load object always traverse meshlet and root node id. 
            objectInfo        = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
            primitiveInfo     = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
            primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
            materialInfo      = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

            localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
            localToView = mul(perView.translatedWorldToView, localToTranslatedWorld);
            localToClip = mul(perView.translatedWorldToClip, localToTranslatedWorld);
        }

        // Object valid.
        if (!bAllBVHNodeChecked && objectId != kUnvalidIdUint32)
        {
            ByteAddressBuffer meshletBuffer             = ByteAddressBindless(primitiveDataInfo.meshletBuffer);
            ByteAddressBuffer meshletGroupBuffer        = ByteAddressBindless(primitiveDataInfo.meshletGroupBuffer);
            ByteAddressBuffer meshletGroupIndicesBuffer = ByteAddressBindless(primitiveDataInfo.meshletGroupIndicesBuffer);

            if (nodeId != kUnvalidIdUint32)
            {
                // Consume and produce node.
                const bool bRootNode = (nodeId == 0);
                const uint loadNodeId = nodeId + primitiveInfo.bvhNodeOffset;
                const GPUBVHNode node = BATL(GPUBVHNode, primitiveDataInfo.bvhNodeBuffer, loadNodeId);
        
                // Node visible state.
                const bool bNodeVisible = bRootNode || isNodeVisible(perView, objectInfo, localToView, node.sphere);

                // Update check node count. 
                InterlockedAdd(rwCounterBuffer[kBVHNodeCheckNodeCountPos], bNodeVisible ? -1 : -node.bvhNodeCount);
                if (bNodeVisible)
                {
                    uint childNode[kNaniteBVHLevelNodeCount];
                    uint produceNewNodeCount = 0;

                    for (uint childId = 0; childId < kNaniteBVHLevelNodeCount; childId ++)
                    {
                        const uint child = node.children[childId];
                        if (child == kUnvalidIdUint32)
                        {
                            continue;
                        }

                        childNode[produceNewNodeCount] = child;
                        produceNewNodeCount ++;
                    }

                    int vramCount = produceNewNodeCount;
                    const uint awakeNewThreadCount = kWaveSize; 
                    uint sharedBaseId = 0;
                    if (produceNewNodeCount > 0)
                    {
                        InterlockedAdd(sharedNodeCount, produceNewNodeCount, sharedBaseId);

                        if (sharedBaseId < awakeNewThreadCount)
                        {
                            uint objectCount = min(sharedBaseId + produceNewNodeCount, awakeNewThreadCount);
                            for (uint i = sharedBaseId; i < objectCount; i ++)
                            {
                                sharedNodeId[i] = childNode[i - sharedBaseId];
                            }   

                            vramCount = (sharedBaseId + produceNewNodeCount) > awakeNewThreadCount ? (sharedBaseId + produceNewNodeCount - awakeNewThreadCount) : 0;
                            sharedBaseId = objectCount - sharedBaseId;
                        }
                        else
                        {
                            sharedBaseId = 0;
                        }
                    }

                    if (vramCount > 0)
                    {
                        // Produce new node.
                        int produceId;
                        InterlockedAdd(rwCounterBuffer[kBVHNodeProduceCountPos], vramCount, produceId);

                        uint2 fillCmd;
                        for (uint newNodeId = 0; newNodeId < vramCount; newNodeId ++)
                        {
                            InterlockedExchange(rwBVHNodeIdBuffer[newNodeId + produceId].x, objectId, fillCmd.x);
                            InterlockedExchange(rwBVHNodeIdBuffer[newNodeId + produceId].y, childNode[sharedBaseId + newNodeId], fillCmd.y);
                        }
                    }

                    // Meshlet handle. 
                    for (uint i = 0; i < node.leafMeshletGroupCount; i ++)
                    {  
                        const uint meshletGroupLoadIndex = primitiveInfo.meshletGroupOffset + i + node.leafMeshletGroupOffset;
                        const GPUGLTFMeshletGroup meshletGroup = meshletGroupBuffer.TypeLoad(GPUGLTFMeshletGroup, meshletGroupLoadIndex);
                        
                        if (isMeshletGroupVisibile(perView, objectInfo, localToTranslatedWorld, localToView, localToClip, meshletGroup))
                        {
                            for (uint j = 0; j < meshletGroup.meshletCount; j ++)
                            {
                                const uint meshletIndicesLoadId = j + meshletGroup.meshletOffset + primitiveInfo.meshletGroupIndicesOffset;
                                const uint meshletIndex = primitiveInfo.meshletOffset + meshletGroupIndicesBuffer.TypeLoad(uint, meshletIndicesLoadId);
                                const GPUGLTFMeshlet meshlet = meshletBuffer.TypeLoad(GPUGLTFMeshlet, meshletIndex);

                                if (isMeshletVisible(perView, objectInfo, localToTranslatedWorld, localToView, localToClip, meshlet, materialInfo))
                                {
                                    uint meshletStoreId = interlockedAddUint(pushConsts.drawedMeshletCountId);
                                    const uint2 drawCmd = uint2(objectId, meshletIndex);
                                    BATS(uint2, pushConsts.drawedMeshletCmdId, meshletStoreId, drawCmd);
                                }
                            }
                        }
                    }
                }
            }
        }
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

    const uint2 drawCmd = BATL(uint2, pushConsts.drawedMeshletCmdId, threadId);
    uint objectId  = drawCmd.x;
    uint meshletId = drawCmd.y;

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

    // Project to prev frame and do some occlusion culling. 
    bool bVisible = true;
    if (shaderHasFlag(pushConsts.switchFlags, kHZBCullingEnableBit))
    {
        float3 maxUVz = -10.0;
        float3 minUVz =  10.0;

        const float4x4 mvp = 
        #if DIM_HZB_CULLING_PHASE_0
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

        if (maxUVz.z < 1.0f && minUVz.z > 0.0f)
        {
            // Clamp min&max uv.
            minUVz.xy = saturate(minUVz.xy);
            maxUVz.xy = saturate(maxUVz.xy);

            // UV convert to hzb space.
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

                const uint2 minPos = uint2(minUVz.xy * mipSize);
                const uint2 maxPos = uint2(maxUVz.xy * mipSize);

                float zMin = hzbTexture.Load(int3(minPos, mipLevel));
                                            zMin = min(zMin, hzbTexture.Load(int3(maxPos, mipLevel)));
                if (maxPos.x != minPos.x) { zMin = min(zMin, hzbTexture.Load(int3(maxPos.x, minPos.y, mipLevel))); }
                if (maxPos.y != minPos.y) { zMin = min(zMin, hzbTexture.Load(int3(minPos.x, maxPos.y, mipLevel))); }
                if (zMin > maxUVz.z)
                {
                    // Occluded by hzb.
                    bVisible = false;
                }
            }
        }
    }

    // If visible, add to draw list.
    if (bVisible)
    {
        uint drawCmdId = interlockedAddUint(pushConsts.drawedMeshletCountId_1);
        BATS(uint2, pushConsts.drawedMeshletCmdId_1, drawCmdId, drawCmd);

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
        uint drawCmdId = interlockedAddUint(pushConsts.drawedMeshletCountId_2);
        BATS(uint2, pushConsts.drawedMeshletCmdId_2, drawCmdId, drawCmd);
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

    uint2 drawCmd;
    GPUGLTFMeshlet meshlet;
    if (bVisible)
    {
        drawCmd = BATL(uint2, pushConsts.drawedMeshletCmdId, threadId);
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

    #if DIM_MESH_SHADER
        BATS(uint2, pushConsts.drawedMeshletCmdId_1, drawCmdId, drawCmd);
    #else 
        // Vertex shader fallback. 
        GLTFMeshletDrawCmd visibleDrawCmd;
        visibleDrawCmd.vertexCount    = unpackTriangleCount(meshlet.vertexTriangleCount) * 3;
        visibleDrawCmd.instanceCount  = 1;
        visibleDrawCmd.firstVertex    = 0;
        visibleDrawCmd.firstInstance  = drawCmdId;
        visibleDrawCmd.objectId       = drawCmd.x;
        visibleDrawCmd.meshletId      = drawCmd.y;
        BATS(GLTFMeshletDrawCmd, pushConsts.drawedMeshletCmdId_1, drawCmdId, visibleDrawCmd);
    #endif
    }
}

#endif // !__cplusplus