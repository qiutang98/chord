#include "gltf.h"

struct GLTFMeshDrawCmd
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
    uint objectId;
    uint packLodMeshlet;
};

#define kLODEnableBit            0
#define kFrustumCullingEnableBit 1
#define kHZBCullingEnableBit     2

struct GLTFDrawPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(GLTFDrawPushConsts);

    uint cameraViewId;
    uint debugFlags;
    uint switchFlags;
    uint meshletCullGroupCountId;
    uint meshletCullGroupDetailId;
    uint meshletCullCmdId;

    uint hzb; // hzb texture id.  
    uint hzbMipCount;
    float uv2HzbX;
    float uv2HzbY;
    uint hzbMip0Width;
    uint hzbMip0Height;

    uint drawedMeshletCountId;
    uint drawedMeshletCmdId;
    uint drawedMeshletCountId_1;
    uint drawedMeshletCmdId_1;
    uint drawedMeshletCountId_2;
    uint drawedMeshletCmdId_2;
};
CHORD_PUSHCONST(GLTFDrawPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"
#include "debug_line.hlsli"

[numthreads(64, 1, 1)]
void perobjectCullingCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;  

    if (threadId >= scene.GLTFObjectCount)
    {
        return;
    } 

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, threadId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    float4x4 translatedWorldToLocal = objectInfo.basicData.translatedWorldToLocal;

    float4x4 translatedWorldToView = perView.translatedWorldToView;
    float4x4 viewToClip = perView.viewToClip;

    const float4 positionAverageLS = float4(primitiveInfo.posAverage, 1.0);
    const float4 positionAverageRS = mul(localToTranslatedWorld, positionAverageLS);
//   const float4 positionAverageVS = mul(translatedWorldToView, positionAverageRS);

    // Camera to average position distance.
    const float cameraToPositionAverageVS = length(positionAverageRS.xyz);

    const float3 posMin = primitiveInfo.posMin;
    const float3 posMax = primitiveInfo.posMax;
    const float3 posCenter = (posMin + posMax) * 0.5;
    const float3 extent = posMax - posCenter;

    // Frustum visible culling: use obb.
    if (shaderHasFlag(pushConsts.switchFlags, kFrustumCullingEnableBit))
    {
        if (frustumCulling(perView.frustumPlane, posCenter, extent, localToTranslatedWorld))
        {
            return;
        }
    }

    // LOD selected. 
    uint selectedLod = 0;
    if (shaderHasFlag(pushConsts.switchFlags, kLODEnableBit))
    {
        // https://www.desmos.com/calculator/u0jgsic77y
        float lodIndex = log2(cameraToPositionAverageVS * primitiveInfo.lodBase) * primitiveInfo.loadStep + 1.0;
        selectedLod = clamp(uint(lodIndex), 0, primitiveInfo.lodCount - 1);
    }
    
    const GPUGLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLod];
    const uint dispatchCullGroupCount = (lodInfo.meshletCount + 63) / 64;
    const uint meshletBaseOffset = interlockedAddUint(pushConsts.meshletCullGroupCountId, dispatchCullGroupCount);

    RWByteAddressBuffer meshletCullGroupBuffer = RWByteAddressBindless(pushConsts.meshletCullGroupDetailId);
    for (uint i = 0; i < dispatchCullGroupCount; i ++)
    {
        const uint id = i + meshletBaseOffset;
        const uint meshletBaseIndex = lodInfo.firstMeshlet + i * 64;

        // Fill meshlet dispatch param, pack to 64.
        uint2 result = uint2(threadId, packLodMeshlet(selectedLod, meshletBaseIndex));
        meshletCullGroupBuffer.TypeStore(uint2, id, result);
    }
}

[numthreads(1, 1, 1)]
void fillMeshletCullCmdCS()
{
    const uint meshletGroupCount = BATL(uint, pushConsts.meshletCullGroupCountId, 0);
    uint4 cmdParameter;
    cmdParameter.x = meshletGroupCount;
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;

    BATS(uint4, pushConsts.meshletCullCmdId, 0, cmdParameter);
}

[numthreads(64, 1, 1)]
void meshletCullingCS(uint groupID : SV_GroupID, uint groupThreadID : SV_GroupThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint meshletGroupCount = BATL(uint, pushConsts.meshletCullGroupCountId, 0);
    const uint2 meshletGroup =  BATL(uint2, pushConsts.meshletCullGroupDetailId, groupID);

    uint objectId = meshletGroup.x;
    uint selectedLod;
    uint meshletId;
    {
        unpackLodMeshlet(meshletGroup.y, selectedLod, meshletId);
        meshletId += groupThreadID;
    }

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GPUGLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLod];

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;

    // Skip out of range meshlt. 
    if (meshletId >= lodInfo.firstMeshlet + lodInfo.meshletCount)
    {
        return;
    }

    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    const float3 posMin = meshlet.posMin;
    const float3 posMax = meshlet.posMax;
    const float3 posCenter = (posMin + posMax) * 0.5;
    const float3 extent = posMax - posCenter;

    // Frustum visible culling: use obb.
    if (shaderHasFlag(pushConsts.switchFlags, kFrustumCullingEnableBit))
    {
        if (frustumCulling(perView.frustumPlane, posCenter, extent, localToTranslatedWorld))
        {
            return;
        }
    }

    uint drawCmdId = interlockedAddUint(pushConsts.drawedMeshletCountId);

    // Fill in draw cmd. 
    {
        GLTFMeshDrawCmd drawCmd;

        drawCmd.vertexCount    = meshlet.triangleCount * 3;
        drawCmd.instanceCount  = 1;
        drawCmd.firstVertex    = 0;
        drawCmd.firstInstance  = drawCmdId;
        drawCmd.objectId       = objectId;
        drawCmd.packLodMeshlet = packLodMeshlet(selectedLod, meshletId);

        BATS(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, drawCmdId, drawCmd);
    }
}

[numthreads(1, 1, 1)]
void fillHZBCullParamCS()
{
    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);

    if(pushConsts.debugFlags == 2)
    {
        printf("Meshlet count before hzb: %d.", meshletCount);
    }

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

    SamplerState pointSampler = getPointClampEdgeSampler(perView);

    const GLTFMeshDrawCmd drawCmd = BATL(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, threadId);
    uint objectId = drawCmd.objectId;
    uint selectedLod;
    uint meshletId;
    {
        unpackLodMeshlet(drawCmd.packLodMeshlet, selectedLod, meshletId);
    }

    //
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GPUGLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLod];
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

    GLTFMeshDrawCmd visibleDrawCmd;
    visibleDrawCmd.vertexCount    = meshlet.triangleCount * 3;
    visibleDrawCmd.instanceCount  = 1;
    visibleDrawCmd.firstVertex    = 0;
    visibleDrawCmd.objectId       = objectId;
    visibleDrawCmd.packLodMeshlet = packLodMeshlet(selectedLod, meshletId);

    // If visible, add to draw list.
    if (bVisible)
    {
        uint drawCmdId = interlockedAddUint(pushConsts.drawedMeshletCountId_1);

        visibleDrawCmd.firstInstance  = drawCmdId;
        BATS(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId_1, drawCmdId, visibleDrawCmd);

    #if DIM_PRINT_DEBUG_BOX
        uint packColor = simpleHashColorPack(visibleDrawCmd.packLodMeshlet);
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

        visibleDrawCmd.firstInstance  = drawCmdId;
        BATS(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId_2, drawCmdId, visibleDrawCmd);
    }
#endif
}

struct DepthOnlyVS2PS
{
    float4 positionHS : SV_Position;
#if DIM_MASKED_MATERIAL
    float2 uv : TEXCOORD0;
    nointerpolation uint objectId : TEXCOORD1;
#endif
};
 
void depthOnlyVS(
    uint vertexId : SV_VertexID, 
    uint instanceId : SV_INSTANCEID,
    out DepthOnlyVS2PS output)
{
    const GLTFMeshDrawCmd drawCmd = BATL(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, instanceId);
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    uint objectId = drawCmd.objectId;
    uint selectedLod;
    uint meshletId;
    {
        unpackLodMeshlet(drawCmd.packLodMeshlet, selectedLod, meshletId);
    }

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GPUGLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLod];
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer indicesDataBuffer = ByteAddressBindless(primitiveDataInfo.indicesBuffer);
    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);

    const uint sampleIndices = lodInfo.firstIndex + meshlet.firstIndex + vertexId;
    const uint indicesId = primitiveInfo.vertexOffset + indicesDataBuffer.TypeLoad(uint, sampleIndices);

    // 
    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    float4x4 translatedWorldToClip = perView.translatedWorldToClip;

    const float3 positionLS = positionDataBuffer.TypeLoad(float3, indicesId);
    const float4 positionRS = mul(localToTranslatedWorld, float4(positionLS, 1.0));

    output.positionHS = mul(translatedWorldToClip, positionRS);
    
#if DIM_MASKED_MATERIAL
    ByteAddressBuffer uvDataBuffer = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);
    output.uv = uvDataBuffer.TypeLoad(float2, indicesId);//
    output.objectId = objectId;
#endif
} 

void depthOnlyPS(in DepthOnlyVS2PS input, out float4 outSceneColor : SV_Target0)
{
    outSceneColor = float4(0.0, 0.0, 0.0, 1.0);

#if DIM_MASKED_MATERIAL
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, input.objectId);
    const GLTFMaterialGPUData materialInfo = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
    SamplerState baseColorSampler = Bindless(SamplerState, materialInfo.baseColorSampler);

    float4 sampleColor = baseColorTexture.Sample(baseColorSampler, input.uv) * materialInfo.baseColorFactor;
    if (sampleColor.w < materialInfo.alphaCutOff)
    {
        discard;
    }
#endif
}

struct BasePassVS2PS
{
    float4 positionHS : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint objectId : TEXCOORD1;
};

void basepassVS(
    uint vertexId : SV_VertexID, 
    uint instanceId : SV_INSTANCEID,
    out BasePassVS2PS output)
{
    const GLTFMeshDrawCmd drawCmd = BATL(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, instanceId);
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    uint objectId = drawCmd.objectId;
    uint selectedLod;
    uint meshletId;
    {
        unpackLodMeshlet(drawCmd.packLodMeshlet, selectedLod, meshletId);
    }

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GPUGLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLod];
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer indicesDataBuffer = ByteAddressBindless(primitiveDataInfo.indicesBuffer);
    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer uvDataBuffer = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);

    const uint sampleIndices = lodInfo.firstIndex + meshlet.firstIndex + vertexId;
    const uint indicesId = primitiveInfo.vertexOffset + indicesDataBuffer.TypeLoad(uint, sampleIndices);

    // 
    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    float4x4 translatedWorldToView = perView.translatedWorldToView;
    float4x4 viewToClip = perView.viewToClip;
    float4x4 translatedWorldToClip = perView.translatedWorldToClip;

    float3 rawPosition = positionDataBuffer.TypeLoad(float3, indicesId);
    float4 positionLS = float4(rawPosition, 1.0);
    float4 positionRS = mul(localToTranslatedWorld, positionLS);

    // Keep same with prepass, if no, will z fighting. 
    // Exist some simd instructions here, like fma, and result diff from cpp. 
    output.positionHS = mul(translatedWorldToClip, positionRS); 
    output.uv = uvDataBuffer.TypeLoad(float2, indicesId);//
    output.objectId = objectId;
}
 
// Draw pixel color.
void basepassPS(  
    in BasePassVS2PS input, 
    out float4 outSceneColor : SV_Target0)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, input.objectId);
    const GLTFMaterialGPUData materialInfo = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
    SamplerState baseColorSampler = Bindless(SamplerState, materialInfo.baseColorSampler);

    float4 sampleColor = baseColorTexture.Sample(baseColorSampler, input.uv) * materialInfo.baseColorFactor;

    outSceneColor.xyz = sampleColor.xyz;
    outSceneColor.a = 1.0;
}

#endif // !__cplusplus