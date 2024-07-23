#include "gltf.h"

struct GLTFMeshDrawCmd
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint objectId;
    uint packLodMeshlet;
};

struct GLTFDrawPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(GLTFDrawPushConsts);

    uint cameraViewId;
    uint debugFlags;

    // Drawing which object. 
    uint meshletCullGroupCountId;
    uint meshletCullGroupDetailId; // draw lod.
    uint meshletCullCmdId;
    uint drawedMeshletCountId;
    uint drawedMeshletCmdId;
};
CHORD_PUSHCONST(GLTFDrawPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "debug.hlsli"

uint packLodMeshlet(uint lod, uint meshlet)
{
    return (lod & 0xF) | ((meshlet & 0xFFFFFFF) << 4);
}

void unpackLodMeshlet(uint packData, out uint lod, out uint meshlet)
{
    lod     = packData & 0xF;
    meshlet = packData >> 4;
}

[numthreads(64, 1, 1)]
void perobjectCullingCS(uint lane : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    if (lane >= scene.GLTFObjectCount)
    {
        return;
    }

    const GPUObjectGLTFPrimitive objectInfo = ByteAddressBindless(scene.GLTFObjectBuffer).Load<GPUObjectGLTFPrimitive>(lane * sizeof(GPUObjectGLTFPrimitive));
    const GLTFPrimitiveBuffer primitiveInfo = ByteAddressBindless(scene.GLTFPrimitiveDetailBuffer).Load<GLTFPrimitiveBuffer>(objectInfo.GLTFPrimitiveDetail * sizeof(GLTFPrimitiveBuffer));
    
    bool bVisible = true;
    {
        // Visible culling. 
    }

    if (!bVisible)
    {
        return;
    }

    uint selectedLod = 0;
    {
        // Lod select.
    }

    const GPUGLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLod];
    const uint dispatchCullGroupCount = (lodInfo.meshletCount + 63) / 64;

    uint meshletBaseOffset;
    {
        RWStructuredBuffer<uint> meshletCountBuffer = TBindless(RWStructuredBuffer, uint, pushConsts.meshletCullGroupCountId);
        InterlockedAdd(meshletCountBuffer[0], dispatchCullGroupCount, meshletBaseOffset);
    }

    RWStructuredBuffer<uint2> meshletCullGroupBuffer = TBindless(RWStructuredBuffer, uint2, pushConsts.meshletCullGroupDetailId);
    for (uint i = 0; i < dispatchCullGroupCount; i ++)
    {
        const uint id = i + meshletBaseOffset;
        const uint meshletBaseIndex = lodInfo.firstMeshlet + i * 64;

        // Fill meshlet dispatch param, pack to 64.
        meshletCullGroupBuffer[id] = uint2(lane, packLodMeshlet(selectedLod, meshletBaseIndex));
    }
}

[numthreads(1, 1, 1)]
void fillMeshletCullCmdCS()
{
    StructuredBuffer<uint> meshletCountBuffer = TBindless(StructuredBuffer, uint, pushConsts.meshletCullGroupCountId);
    const uint meshletGroupCount = meshletCountBuffer[0];

    uint4 cmdParameter;
    cmdParameter.x = meshletGroupCount;
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;

    RWStructuredBuffer<uint4> meshletCullCmdBuffer = TBindless(RWStructuredBuffer, uint4, pushConsts.meshletCullCmdId);
    meshletCullCmdBuffer[0] = cmdParameter;
}

[numthreads(64, 1, 1)]
void meshletCullingCS(uint groupID : SV_GroupID, uint groupThreadID : SV_GroupThreadID)
{
    StructuredBuffer<uint> meshletCountBuffer = TBindless(StructuredBuffer, uint, pushConsts.meshletCullGroupCountId);
    const uint meshletGroupCount = meshletCountBuffer[0];

    StructuredBuffer<uint2> meshletCullGroupBuffer = TBindless(StructuredBuffer, uint2, pushConsts.meshletCullGroupDetailId);
    const uint2 meshletGroup = meshletCullGroupBuffer[groupID];

    uint objectId = meshletGroup.x;
    uint selectedLod;
    uint meshletId;
    {
        unpackLodMeshlet(meshletGroup.y, selectedLod, meshletId);
        meshletId += groupThreadID;
    }

    const GPUObjectGLTFPrimitive objectInfo = ByteAddressBindless(scene.GLTFObjectBuffer).Load<GPUObjectGLTFPrimitive>(objectId * sizeof(GPUObjectGLTFPrimitive));
    const GLTFPrimitiveBuffer primitiveInfo = ByteAddressBindless(scene.GLTFPrimitiveDetailBuffer).Load<GLTFPrimitiveBuffer>(objectInfo.GLTFPrimitiveDetail * sizeof(GLTFPrimitiveBuffer));
    const GPUGLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLod];

    // Skip out of range meshlt. 
    if (meshletId >= lodInfo.firstMeshlet + lodInfo.meshletCount)
    {
        return;
    }

    const GLTFPrimitiveDatasBuffer primitiveDataInfo = ByteAddressBindless(scene.GLTFPrimitiveDataBuffer).Load<GLTFPrimitiveDatasBuffer>(primitiveInfo.primitiveDatasBufferId * sizeof(GLTFPrimitiveDatasBuffer));
    const GPUGLTFMeshlet meshlet = ByteAddressBindless(primitiveDataInfo.meshletBuffer).Load<GPUGLTFMeshlet>(meshletId * sizeof(GPUGLTFMeshlet));

    bool bVisible = true;
    {
        // Visible culling. 
    }

    if (!bVisible)
    {
        return;
    }

    uint drawCmdId;
    {
        RWStructuredBuffer<uint> drawedCountBuffer = TBindless(RWStructuredBuffer, uint, pushConsts.drawedMeshletCountId);
        InterlockedAdd(drawedCountBuffer[0], 1, drawCmdId);
    }

    // Fill in draw cmd. 
    {
        GLTFMeshDrawCmd drawCmd;

        drawCmd.vertexCount    = meshlet.triangleCount * 3;
        drawCmd.instanceCount  = 1;
        drawCmd.firstVertex    = 0;
        drawCmd.objectId       = objectId;
        drawCmd.packLodMeshlet = packLodMeshlet(selectedLod, meshletId);

        RWByteAddressBindless(pushConsts.drawedMeshletCmdId).Store<GLTFMeshDrawCmd>(drawCmdId * sizeof(GLTFMeshDrawCmd), drawCmd);
    }
}

struct BasePassVS2PS
{
    float4 positionHS : SV_Position;
    float3 positionVS : POSITION0;
    float3 normalVS   : NORMAL0;
};

void mainVS(
    uint vertexId : SV_VertexID, 
    uint primitiveId : SV_PRIMITIVEID, 
    uint instanceId : SV_INSTANCEID,
    out BasePassVS2PS output)
{
    const GLTFMeshDrawCmd drawCmd = ByteAddressBindless(pushConsts.drawedMeshletCmdId).Load<GLTFMeshDrawCmd>(instanceId * sizeof(GLTFMeshDrawCmd));
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);

    uint objectId = drawCmd.objectId;
    uint selectedLod;
    uint meshletId;
    {
        unpackLodMeshlet(drawCmd.packLodMeshlet, selectedLod, meshletId);
    }

    const GPUObjectGLTFPrimitive objectInfo = ByteAddressBindless(scene.GLTFObjectBuffer).Load<GPUObjectGLTFPrimitive>(objectId * sizeof(GPUObjectGLTFPrimitive));
    const GLTFPrimitiveBuffer primitiveInfo = ByteAddressBindless(scene.GLTFPrimitiveDetailBuffer).Load<GLTFPrimitiveBuffer>(objectInfo.GLTFPrimitiveDetail * sizeof(GLTFPrimitiveBuffer));
    const GPUGLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLod];
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = ByteAddressBindless(scene.GLTFPrimitiveDataBuffer).Load<GLTFPrimitiveDatasBuffer>(primitiveInfo.primitiveDatasBufferId * sizeof(GLTFPrimitiveDatasBuffer));
    const GPUGLTFMeshlet meshlet = ByteAddressBindless(primitiveDataInfo.meshletBuffer).Load<GPUGLTFMeshlet>(meshletId * sizeof(GPUGLTFMeshlet));

    ByteAddressBuffer indicesDataBuffer = ByteAddressBindless(primitiveDataInfo.indicesBuffer);
    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer normalDataBuffer = ByteAddressBindless(primitiveDataInfo.normalBuffer);

    const uint indicesId = indicesDataBuffer.Load<uint>((meshlet.firstIndex + vertexId) * sizeof(uint));
    // 
    float4x4 localToWorld = objectInfo.basicData.localToWorld;
    float4x4 translatedWorldToView = perView.translatedWorldToView;
    float4x4 viewToClip = perView.viewToClip;

    float4 positionLS = float4(positionDataBuffer.Load<float3>(indicesId * sizeof(float3)), 1.0);
    float4 positionWS = mul(localToWorld, positionLS);
    float4 positionVS = mul(translatedWorldToView, positionWS);
    float4 positionHS = mul(viewToClip, positionVS);

    output.positionVS = positionVS.xyz;
    output.positionHS = positionHS;

    // TODO: Viewspace normal.
    output.normalVS   = normalDataBuffer.Load<float3>(indicesId * sizeof(float3));
}

// Draw pixel color.
void mainPS(
    in BasePassVS2PS input,
    out float4 outSceneColor : SV_Target0)
{
    outSceneColor.xyz = input.normalVS * 0.5 + 0.5;
    outSceneColor.a = 1.0;
}

#endif