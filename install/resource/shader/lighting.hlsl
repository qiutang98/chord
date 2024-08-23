#include "gltf.h"



struct LightingPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(LightingPushConsts);

    float2 visibilityTexelSize;
    uint2 visibilityDim;
    uint cameraViewId;
    uint tileBufferCmdId;
    uint visibilityId;
    uint sceneColorId;
};
CHORD_PUSHCONST(LightingPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"

float3 gltfMetallicRoughnessPBR(uint objectId, uint meshletId, uint triangleId, uint2 dispatchPos)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer meshletDataBuffer = ByteAddressBindless(primitiveDataInfo.meshletDataBuffer);
    uint verticesCount = unpackVertexCount(meshlet.vertexTriangleCount);
    uint triangleIndicesSampleOffset = (meshlet.dataOffset + verticesCount + triangleId) * 4;
    uint indexTri = meshletDataBuffer.Load(triangleIndicesSampleOffset);

    uint verticesSampleOffset_0 = (meshlet.dataOffset + ((indexTri >> (0 * 8)) & 0xff)) * 4;
    uint verticesSampleOffset_1 = (meshlet.dataOffset + ((indexTri >> (1 * 8)) & 0xff)) * 4;
    uint verticesSampleOffset_2 = (meshlet.dataOffset + ((indexTri >> (2 * 8)) & 0xff)) * 4;

    uint verticesIndex_0 = meshletDataBuffer.Load(verticesSampleOffset_0);
    uint verticesIndex_1 = meshletDataBuffer.Load(verticesSampleOffset_1);
    uint verticesIndex_2 = meshletDataBuffer.Load(verticesSampleOffset_2);

    const uint indicesId_0 = primitiveInfo.vertexOffset + verticesIndex_0;
    const uint indicesId_1 = primitiveInfo.vertexOffset + verticesIndex_1;
    const uint indicesId_2 = primitiveInfo.vertexOffset + verticesIndex_2;

    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer uvDataBuffer = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);

    const float3 positionLS_0 = positionDataBuffer.TypeLoad(float3, indicesId_0);
    const float3 positionLS_1 = positionDataBuffer.TypeLoad(float3, indicesId_1);
    const float3 positionLS_2 = positionDataBuffer.TypeLoad(float3, indicesId_2);

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    float4x4 translatedWorldToClip = perView.translatedWorldToClip;
    float4x4 localToClip = mul(translatedWorldToClip, localToTranslatedWorld);

    const float2 uv_0 = uvDataBuffer.TypeLoad(float2, indicesId_0);
    const float2 uv_1 = uvDataBuffer.TypeLoad(float2, indicesId_1);
    const float2 uv_2 = uvDataBuffer.TypeLoad(float2, indicesId_2);

    const GLTFMaterialGPUData materialInfo = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    const float2 UV = (dispatchPos + 0.5) * pushConsts.visibilityTexelSize;

    float4 positionCS_0 = mul(localToClip, float4(positionLS_0, 1.0));
    float4 positionCS_1 = mul(localToClip, float4(positionLS_1, 1.0));
    float4 positionCS_2 = mul(localToClip, float4(positionLS_2, 1.0));

    Barycentrics barycentricCtx = calculateTriangleBarycentrics(screenUvToNdcUv(UV), positionCS_0, positionCS_1, positionCS_2, pushConsts.visibilityTexelSize);
    const float3 barycentric = barycentricCtx.interpolation;
    const float3 ddx = barycentricCtx.ddx;
    const float3 ddy = barycentricCtx.ddy;

    float2 meshUv = uv_0 * barycentric.x + uv_1 * barycentric.y + uv_2 * barycentric.z;
    float2 meshUv_ddx = uv_0 * ddx.x + uv_1 * ddx.y + uv_2 * ddx.z;
    float2 meshUv_ddy = uv_0 * ddy.x + uv_1 * ddy.y + uv_2 * ddy.z;

    Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
    SamplerState baseColorSampler = Bindless(SamplerState, materialInfo.baseColorSampler);

    float4 sampleColor = baseColorTexture.SampleGrad(baseColorSampler, meshUv, meshUv_ddx, meshUv_ddy) * materialInfo.baseColorFactor * 1.5;

#if 0
    if(meshlet.lod == 0) { return float3(1.0, 0.0, 0.0); }
    if(meshlet.lod == 1) { return float3(1.0, 0.0, 1.0); }
    if(meshlet.lod == 2) { return float3(0.0, 0.0, 1.0); }
    if(meshlet.lod == 3) { return float3(0.0, 1.0, 1.0); }
    if(meshlet.lod == 4) { return float3(1.0, 1.0, 0.0); }
    if(meshlet.lod == 5) { return float3(0.0, 1.0, 0.0); }
#endif
    return sampleColor.xyz;
    // return simpleHashColor(triangleId);
    return simpleHashColor(meshletId);// sampleColor.xyz;
}

float3 noneShading(uint objectId, uint triangleId, uint2 dispatchPos)
{
    return 0;
}

void storeColor(uint2 pos, float3 c)
{
    RWTexture2D<float3> rwSceneColor = TBindless(RWTexture2D, float3, pushConsts.sceneColorId);
    rwSceneColor[pos] = c;
}

[numthreads(64, 1, 1)]
void mainCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    uint2 tileOffset = BATL(uint2, pushConsts.tileBufferCmdId, workGroupId);
    uint2 dispatchPos = tileOffset + remap8x8(localThreadIndex);

    // Edge return.
    if (any(dispatchPos >= pushConsts.visibilityDim))
    {
        return;
    }

    Texture2D<uint2> visibilityTexture = TBindless(Texture2D, uint2, pushConsts.visibilityId);
    const uint2 packIDs = visibilityTexture[dispatchPos];

    uint triangleId;
    uint objectId;
    uint shadingType;
    uint meshletId;
    decodeShadingMeshlet(packIDs.x, shadingType, meshletId);
    decodeTriangleIdObjectId(packIDs.y, triangleId, objectId);

    #if LIGHTING_TYPE == kLightingType_None
    {
        if (shadingType != kLightingType_None) { return; }
        storeColor(dispatchPos, noneShading(objectId, triangleId, dispatchPos));
    }
    #elif LIGHTING_TYPE == kLightingType_GLTF_MetallicRoughnessPBR
    {
        if (shadingType != kLightingType_GLTF_MetallicRoughnessPBR) { return; }
        storeColor(dispatchPos, gltfMetallicRoughnessPBR(objectId, meshletId, triangleId, dispatchPos));
    }
    #endif 
}


#endif // !__cplusplus