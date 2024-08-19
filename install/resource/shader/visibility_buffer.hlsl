#include "gltf.h"

struct VisibilityBufferPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(VisibilityBufferPushConst);
    uint cameraViewId;
    uint drawedMeshletCmdId;
};
CHORD_PUSHCONST(VisibilityBufferPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"

struct VisibilityPassVS2PS
{
    float4 positionHS : SV_Position;
    nointerpolation uint2 id : TEXCOORD0;
#if DIM_MASKED_MATERIAL
    float2 uv : TEXCOORD1;
#endif
};

void visibilityPassVS(
    uint vertexId : SV_VertexID, 
    uint instanceId : SV_INSTANCEID,
    out VisibilityPassVS2PS output)
{
    const GLTFMeshletDrawCmd drawCmd = BATL(GLTFMeshletDrawCmd, pushConsts.drawedMeshletCmdId, instanceId);
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;


    // Load object and meshlet.
    uint objectId  = drawCmd.objectId;
    uint meshletId = drawCmd.meshletId;
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer meshletDataBuffer = ByteAddressBindless(primitiveDataInfo.meshletDataBuffer);

    // Get current triangle local id.
    const uint triangleId = vertexId / 3;

    // Get vertices count. 
    uint verticesCount = unpackVertexCount(meshlet.vertexTriangleCount);

    // Get meshlet index of triangle. 
    uint triangleIndicesSampleOffset = (meshlet.dataOffset + verticesCount + triangleId) * 4;
    uint indexTri = meshletDataBuffer.Load(triangleIndicesSampleOffset);

    // Noew get vertices id. 
    uint verticesSampleOffset = (meshlet.dataOffset + ((indexTri >> ((vertexId % 3) * 8)) & 0xff)) * 4;
    uint verticesIndex = meshletDataBuffer.Load(verticesSampleOffset);
    const uint indicesId = primitiveInfo.vertexOffset + verticesIndex;

    // 
    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    float4x4 translatedWorldToClip = perView.translatedWorldToClip;

    const float3 positionLS = positionDataBuffer.TypeLoad(float3, indicesId);
    const float4 positionRS = mul(localToTranslatedWorld, float4(positionLS, 1.0));

    output.positionHS = mul(translatedWorldToClip, positionRS);
    
#if DIM_MASKED_MATERIAL
    ByteAddressBuffer uvDataBuffer = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);
    output.uv = uvDataBuffer.TypeLoad(float2, indicesId); //
#endif

    output.id.x = encodeShadingMeshlet((uint)(EShadingType::GLTF_MetallicRoughnessPBR), meshletId);
    output.id.y = encodeTriangleIdObjectId(triangleId, objectId);
} 

void visibilityPassPS(in VisibilityPassVS2PS input, out uint2 outId : SV_Target0)
{
#if DIM_MASKED_MATERIAL
    uint triangleId;
    uint objectId;
    decodeTriangleIdObjectId(input.id.y, triangleId, objectId);

    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFMaterialGPUData materialInfo = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
    SamplerState baseColorSampler = Bindless(SamplerState, materialInfo.baseColorSampler);

    float4 sampleColor = baseColorTexture.Sample(baseColorSampler, input.uv) * materialInfo.baseColorFactor;
    clip(sampleColor.w - materialInfo.alphaCutOff);
#endif

    // Output id.
    outId = input.id;
}

#endif