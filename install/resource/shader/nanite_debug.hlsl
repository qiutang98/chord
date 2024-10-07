#include "gltf.h"

#define kNaniteDebugType_Meshlet      0
#define kNaniteDebugType_Triangle     1
#define kNaniteDebugType_LOD          2
#define kNaniteDebugType_LODMeshlet   3
#define kNaniteDebugType_Barycentrics 4

struct NaniteDebugPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(NaniteDebugPushConsts);

    float2 visibilityTexelSize;
    uint visibilityTextureId;
    uint cameraViewId;
    uint drawedMeshletCmdId;

    uint debugType;
};
CHORD_PUSHCONST(NaniteDebugPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "nanite_shared.hlsli"
#include "fullscreen.hlsl"

static const float3 kLODDebugColor[kNaniteMaxLODCount] = 
{
    float3(1.0, 0.0, 0.0),
    float3(0.7, 0.3, 0.0),
    float3(0.4, 0.6, 0.0),
    float3(0.1, 0.9, 0.0),
    float3(0.0, 1.0, 0.2),
    float3(0.0, 0.5, 0.6),
    float3(0.0, 0.1, 0.8),
    float3(0.0, 0.0, 1.0),
    float3(0.1, 0.1, 0.8),
    float3(0.2, 0.2, 0.6),
    float3(0.0, 0.4, 0.7),
    float3(0.2, 0.6, 0.3),
};

void mainPS(
    in FullScreenVS2PS input, 
    out float4 outColor : SV_Target0)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    Texture2D<uint> visibilityTexture = TBindless(Texture2D, uint, pushConsts.visibilityTextureId);
    SamplerState pointClampSampler = getPointClampEdgeSampler(perView);

    uint packID = visibilityTexture.SampleLevel(pointClampSampler, input.uv, 0);

    // No thing. 
    if (packID == 0)
    {
        outColor = float4(0, 0, 0, 1);
        return;
    }

    uint triangleId;
    uint instanceId;
    decodeTriangleIdInstanceId(packID, triangleId, instanceId);

    const uint3 drawCmd = BATL(uint3, pushConsts.drawedMeshletCmdId, instanceId);
    check(drawCmd.z == instanceId);

    const uint objectId  = drawCmd.x;
    const uint meshletId = drawCmd.y;

    GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);
    const bool bExistNormalTexture = (materialInfo.normalTexture != kUnvalidIdUint32);

    TriangleMiscInfo triangleInfo;
    const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    const float4x4 translatedWorldToClip  = perView.translatedWorldToClip;
    const float4x4 localToClip            = mul(translatedWorldToClip, localToTranslatedWorld);

    uint triangleIndexId;
    const GPUGLTFMeshlet meshlet = getTriangleMiscInfo(
        scene, 
        objectInfo, 
        bExistNormalTexture, 
        localToClip, 
        meshletId, 
        triangleId, 
        triangleInfo,
        triangleIndexId);

    Barycentrics barycentricCtx = calculateTriangleBarycentrics(
        screenUvToNdcUv(input.uv), 
        triangleInfo.positionHS[0], 
        triangleInfo.positionHS[1], 
        triangleInfo.positionHS[2], 
        pushConsts.visibilityTexelSize);

    const float3 barycentric = barycentricCtx.interpolation;

    const float3 meshletHashColor = simpleHashColor(meshletId);
    const float3 lodColor = kLODDebugColor[meshlet.lod];

    outColor.w = 1.0;
    if (pushConsts.debugType == kNaniteDebugType_Meshlet)
    {
        outColor.xyz = meshletHashColor;
    }
    else if (pushConsts.debugType == kNaniteDebugType_Triangle)
    {
        outColor.xyz = simpleHashColor(triangleIndexId);
    }
    else if (pushConsts.debugType == kNaniteDebugType_LOD)
    {
        outColor.xyz = lodColor;
    }
    else if (pushConsts.debugType == kNaniteDebugType_LODMeshlet)
    {
        outColor.xyz = pow(meshletHashColor, 0.5) * lodColor;
    }
    else if (pushConsts.debugType == kNaniteDebugType_Barycentrics)
    {
        outColor.xyz = barycentric;
    }
}

#endif 