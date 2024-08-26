#include "gltf.h"

#define ENABLE_WAVE_LOCAL_SHUFFLE 1

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

#if ENABLE_WAVE_LOCAL_SHUFFLE
    groupshared uint sharedPackId[64];
#endif

struct TriangleMiscInfo
{
    float2 uv[3];
    float4 positionHS[3];
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
    in const uint sharedIdOffset,
    in const uint packId,
    in const uint meshletId, 
    in const uint triangleId, 
    in const uint2 dispatchPos)
{
    const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    const float4x4 translatedWorldToClip  = perView.translatedWorldToClip;
    const float4x4 localToClip            = mul(translatedWorldToClip, localToTranslatedWorld);

    TriangleMiscInfo triangleInfo;
#if ENABLE_WAVE_LOCAL_SHUFFLE
    const uint currentLaneIndex = WaveGetLaneIndex();
    uint targetLaneIndex;

    uint activeMask = WaveActiveBallot(true).x;
    while (activeMask != 0)
    {
        targetLaneIndex = firstbitlow(activeMask);
        if (targetLaneIndex >= currentLaneIndex)
        {
            break; // Reach edge. 
        }
                     
        // WaveReadLaneAt in loop never work.
        const uint targetPackId = sharedPackId[sharedIdOffset + targetLaneIndex];
        if (targetPackId == packId)
        {
            break; // Found reuse.
        }

        // Step next.
        activeMask ^= (1U << targetLaneIndex);
    }

    // Load triangle info.

    [branch]
    if (currentLaneIndex == targetLaneIndex)
    {
        getTriangleMiscInfo(perView, scene, objectInfo, localToClip, meshletId, triangleId, triangleInfo);
    }

    [unroll(3)] 
    for (uint i = 0; i < 3; i ++)
    {
        triangleInfo.uv[i] = WaveReadLaneAt(triangleInfo.uv[i], targetLaneIndex);
        triangleInfo.positionHS[i] = WaveReadLaneAt(triangleInfo.positionHS[i], targetLaneIndex);
    }
#else 
    getTriangleMiscInfo(perView, scene, objectInfo, localToClip, meshletId, triangleId, triangleInfo);
#endif

    

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

#if 0
    Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
    SamplerState baseColorSampler      = Bindless(SamplerState, materialInfo.baseColorSampler);

    float4 sampleColor = baseColorTexture.SampleGrad(baseColorSampler, meshUv, meshUv_ddx, meshUv_ddy) * materialInfo.baseColorFactor * 1.5;

    if(meshlet.lod == 0) { return float3(1.0, 0.0, 0.0); } 
    if(meshlet.lod == 1) { return float3(1.0, 0.0, 1.0); }
    if(meshlet.lod == 2) { return float3(0.0, 0.0, 1.0); }
    if(meshlet.lod == 3) { return float3(0.0, 1.0, 1.0); }
    if(meshlet.lod == 4) { return float3(1.0, 1.0, 0.0); }
    if(meshlet.lod == 5) { return float3(0.0, 1.0, 0.0); }

    return sampleColor.xyz;
#endif


    // return simpleHashColor(triangleId);
    return abs(barycentric);// sampleColor.xyz; 
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
    uint packID = 0;
    if (all(dispatchPos <= pushConsts.visibilityDim))
    {
        Texture2D<uint> visibilityTexture = TBindless(Texture2D, uint, pushConsts.visibilityId);
        packID = visibilityTexture[dispatchPos];
    }

    const uint sharedIdOffset = (localThreadIndex / 32) * 32;
    const uint sharedId = WaveGetLaneIndex() + sharedIdOffset;

#if ENABLE_WAVE_LOCAL_SHUFFLE
    sharedPackId[sharedId] = packID;
    GroupMemoryBarrierWithGroupSync();
#endif

    if (packID == 0) { return; }

    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    uint triangleId;
    uint instanceId;
    decodeTriangleIdInstanceId(packID, triangleId, instanceId);

    const uint3 drawCmd = BATL(uint3, pushConsts.drawedMeshletCmdId, instanceId);
    check(drawCmd.z == instanceId);

    uint objectId  = drawCmd.x;
    uint meshletId = drawCmd.y;

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    //
    const uint shadingType = materialInfo.materialType;

    #if LIGHTING_TYPE == kLightingType_GLTF_MetallicRoughnessPBR
    {
        if (shadingType != kLightingType_GLTF_MetallicRoughnessPBR) { return; }
        
        storeColor(dispatchPos, gltfMetallicRoughnessPBR(perView, scene, objectInfo, materialInfo, sharedIdOffset, packID, meshletId, triangleId, dispatchPos));
    }
    #endif 
}


#endif // !__cplusplus