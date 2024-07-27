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

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, lane);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    
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
        RWByteAddressBuffer meshletCountBuffer = RWByteAddressBindless(pushConsts.meshletCullGroupCountId);
        meshletCountBuffer.InterlockedAdd(0, dispatchCullGroupCount, meshletBaseOffset);
    }

    RWByteAddressBuffer meshletCullGroupBuffer = RWByteAddressBindless(pushConsts.meshletCullGroupDetailId);
    for (uint i = 0; i < dispatchCullGroupCount; i ++)
    {
        const uint id = i + meshletBaseOffset;
        const uint meshletBaseIndex = lodInfo.firstMeshlet + i * 64;

        // Fill meshlet dispatch param, pack to 64.
        meshletCullGroupBuffer.TypeStore(uint2, id, uint2(lane, packLodMeshlet(selectedLod, meshletBaseIndex)));
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

    if (pushConsts.debugFlags == 3)
    {
        debug::printFormat(float4(cmdParameter));
    }

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

    // Skip out of range meshlt. 
    if (meshletId >= lodInfo.firstMeshlet + lodInfo.meshletCount)
    {
        return;
    }

    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

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
        RWByteAddressBuffer drawedCountBuffer = RWByteAddressBindless(pushConsts.drawedMeshletCountId);
        drawedCountBuffer.InterlockedAdd(0, 1, drawCmdId);
    }

    // Fill in draw cmd. 
    {
        GLTFMeshDrawCmd drawCmd;

        drawCmd.vertexCount    = meshlet.triangleCount * 3;
        drawCmd.instanceCount  = 1;
        drawCmd.firstVertex    = 0;
        drawCmd.firstInstance  = drawCmdId;
        drawCmd.objectId       = objectId;
        drawCmd.packLodMeshlet = packLodMeshlet(selectedLod, meshletId);

        if (pushConsts.debugFlags == 2 && drawCmdId > 220)
        {
            debug::printFormat(float4(objectId, selectedLod, meshletId, drawCmdId));
        }
        BATS(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, drawCmdId, drawCmd);
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
    ByteAddressBuffer normalDataBuffer = ByteAddressBindless(primitiveDataInfo.normalBuffer);

    const uint indicesId = primitiveInfo.vertexOffset + indicesDataBuffer.TypeLoad(uint, lodInfo.firstIndex + meshlet.firstIndex + vertexId);

    if (pushConsts.debugFlags == 1 && vertexId == 0 && meshletId > 220)
    {
        // debug::printFormat(float4(meshletId, lodInfo.firstIndex, meshlet.firstIndex, meshlet.triangleCount));
        debug::printFormat(float4(meshletId, primitiveInfo.vertexCount, indicesDataBuffer.TypeLoad(uint, lodInfo.firstIndex + meshlet.firstIndex + vertexId), meshlet.triangleCount));
    }

    if (pushConsts.debugFlags == 4 && indicesId > primitiveInfo.vertexCount)
    {

    }

    // 
    float4x4 localToWorld = objectInfo.basicData.localToWorld;
    float4x4 translatedWorldToView = perView.translatedWorldToView;
    float4x4 viewToClip = perView.viewToClip;

    float4 positionLS = float4(positionDataBuffer.TypeLoad(float3, indicesId), 1.0);
    float4 positionWS = mul(localToWorld, positionLS);
    float4 positionVS = mul(translatedWorldToView, positionWS);
    float4 positionHS = mul(viewToClip, positionVS);

    output.positionVS = positionVS.xyz;
    output.positionHS = positionHS;

    // TODO: Viewspace normal.
    output.normalVS = normalDataBuffer.TypeLoad(float3, indicesId);
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