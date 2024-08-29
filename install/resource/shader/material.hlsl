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

    uint drawedMeshletCmdId;
};
CHORD_PUSHCONST(LightingPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"

struct TriangleMiscInfo
{
    float4 positionHS[3];
    float4 tangent[3];
    float3 normal[3];
    float2 uv[3];
};

void getTriangleMiscInfo(
    in const PerframeCameraView perView,
    in const GPUBasicData scene,
    in const GPUObjectGLTFPrimitive objectInfo,
    in const float4x4 localToClip,
    in const uint meshletId, 
    in const uint triangleId,
    out TriangleMiscInfo outTriangle)
{
    const GLTFPrimitiveBuffer primitiveInfo          = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet                     = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer meshletDataBuffer = ByteAddressBindless(primitiveDataInfo.meshletDataBuffer);
    uint verticesCount                  = unpackVertexCount(meshlet.vertexTriangleCount);
    uint triangleIndicesSampleOffset    = (meshlet.dataOffset + verticesCount + triangleId) * 4;
    uint indexTri                       = meshletDataBuffer.Load(triangleIndicesSampleOffset);

    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer uvDataBuffer = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);

    [unroll(3)]
    for(uint i = 0; i < 3; i ++)
    {
        const uint verticesSampleOffset = (meshlet.dataOffset + ((indexTri >> (i * 8)) & 0xff)) * 4;
        const uint indicesId = primitiveInfo.vertexOffset + meshletDataBuffer.Load(verticesSampleOffset);

        // position load and projection.
        const float3 positionLS = positionDataBuffer.TypeLoad(float3, indicesId);
        outTriangle.positionHS[i] = mul(localToClip, float4(positionLS, 1.0));

        //  uv load. 
        outTriangle.uv[i] = uvDataBuffer.TypeLoad(float2, indicesId);
    }
}

float3 gltfMetallicRoughnessPBR(
    in const PerframeCameraView perView,
    in const GPUBasicData scene,
    in const GPUObjectGLTFPrimitive objectInfo,
    in const GLTFMaterialGPUData materialInfo,
    in const TriangleMiscInfo triangleInfo,
    in const uint2 dispatchPos)
{
    // Screen uv. 
    const float2 screenUV = (dispatchPos + 0.5) * pushConsts.visibilityTexelSize;

    //
    Barycentrics barycentricCtx = calculateTriangleBarycentrics(
        screenUvToNdcUv(screenUV), 
        triangleInfo.positionHS[0], 
        triangleInfo.positionHS[1], 
        triangleInfo.positionHS[2], 
        pushConsts.visibilityTexelSize);

    const float3 barycentric = barycentricCtx.interpolation;
    const float3 ddx = barycentricCtx.ddx;
    const float3 ddy = barycentricCtx.ddy;

    float2 meshUv     = triangleInfo.uv[0] * barycentric.x + triangleInfo.uv[1] * barycentric.y + triangleInfo.uv[2] * barycentric.z;
    float2 meshUv_ddx = triangleInfo.uv[0] * ddx.x         + triangleInfo.uv[1] * ddx.y         + triangleInfo.uv[2] * ddx.z;
    float2 meshUv_ddy = triangleInfo.uv[0] * ddy.x         + triangleInfo.uv[1] * ddy.y         + triangleInfo.uv[2] * ddy.z;

    Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
    SamplerState baseColorSampler      = Bindless(SamplerState, materialInfo.baseColorSampler);

    float4 sampleColor = baseColorTexture.SampleGrad(baseColorSampler, meshUv, meshUv_ddx, meshUv_ddy) * materialInfo.baseColorFactor;
#if 0

    if(meshlet.lod == 0) { return float3(1.0, 0.0, 0.0); } 
    if(meshlet.lod == 1) { return float3(1.0, 0.0, 1.0); }
    if(meshlet.lod == 2) { return float3(0.0, 0.0, 1.0); }
    if(meshlet.lod == 3) { return float3(0.0, 1.0, 1.0); }
    if(meshlet.lod == 4) { return float3(1.0, 1.0, 0.0); }
    if(meshlet.lod == 5) { return float3(0.0, 1.0, 0.0); }
#endif

    return sampleColor.xyz;
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
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    Texture2D<uint> visibilityTexture = TBindless(Texture2D, uint, pushConsts.visibilityId);

    //
    uint cachePackId = 0;

    // Cache data.  
    GPUObjectGLTFPrimitive objectInfo;
    GLTFMaterialGPUData    materialInfo;
    TriangleMiscInfo       triangleInfo;

    const uint  sampleGroup = workGroupId * 4 + localThreadIndex / 16;
    const uint2 tileOffset  = BATL(uint2, pushConsts.tileBufferCmdId, sampleGroup);

    [unroll(4)]
    for (uint i = 0; i < 4; i ++)
    {
        const uint2 dispatchPos = tileOffset + remap8x8((localThreadIndex % 16) * 4 + i);

        uint packID = 0;
        if (all(dispatchPos <= pushConsts.visibilityDim))
        {
            packID = visibilityTexture[dispatchPos];
        }

        [branch]
        if (packID != 0 && cachePackId != packID)
        {
            uint triangleId;
            uint instanceId;
            decodeTriangleIdInstanceId(packID, triangleId, instanceId);

            const uint3 drawCmd = BATL(uint3, pushConsts.drawedMeshletCmdId, instanceId);
            check(drawCmd.z == instanceId);

            const uint objectId  = drawCmd.x;
            const uint meshletId = drawCmd.y;

            objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
            materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);


            const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
            const float4x4 translatedWorldToClip  = perView.translatedWorldToClip;
            const float4x4 localToClip            = mul(translatedWorldToClip, localToTranslatedWorld);
            getTriangleMiscInfo(perView, scene, objectInfo, localToClip, meshletId, triangleId, triangleInfo);

            cachePackId = packID;
        }

        //
    #if LIGHTING_TYPE == kLightingType_GLTF_MetallicRoughnessPBR
        [branch]
        if (packID != 0 && materialInfo.materialType == kLightingType_GLTF_MetallicRoughnessPBR)
        {
            storeColor(dispatchPos, gltfMetallicRoughnessPBR(perView, scene, objectInfo, materialInfo, triangleInfo, dispatchPos));
        }
    #endif 
    }
}


#endif // !__cplusplus