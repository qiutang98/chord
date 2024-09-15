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

#include "nanite_shared.hlsli"

float3 gltfMetallicRoughnessPBR(
    in const PerframeCameraView perView,
    in const GPUBasicData scene,
    in const GPUObjectGLTFPrimitive objectInfo,
    in const GLTFMaterialGPUData materialInfo,
    in const TriangleMiscInfo triangleInfo,
    in const bool bExistNormalTexture,
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

    float4 baseColor;
    {
        Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
        SamplerState baseColorSampler      = Bindless(SamplerState, materialInfo.baseColorSampler);
        baseColor = baseColorTexture.SampleGrad(baseColorSampler, meshUv, meshUv_ddx, meshUv_ddy) * materialInfo.baseColorFactor;
    }

    // Vertex normal.
    const float3 vertNormalRS = triangleInfo.normalRS[0] * barycentric.x + triangleInfo.normalRS[1] * barycentric.y + triangleInfo.normalRS[2] * barycentric.z;

    float3 normalRS;
    [branch]
    if (bExistNormalTexture)
    {
        float3 tangentRS     =   triangleInfo.tangentRS[0] * barycentric.x +   triangleInfo.tangentRS[1] * barycentric.y +   triangleInfo.tangentRS[2] * barycentric.z;
        float3 bitangentRS   = triangleInfo.bitangentRS[0] * barycentric.x + triangleInfo.bitangentRS[1] * barycentric.y + triangleInfo.bitangentRS[2] * barycentric.z;

        const float3x3 TBN = float3x3(tangentRS, bitangentRS, vertNormalRS);

        Texture2D<float2> normalTexture = TBindless(Texture2D, float2, materialInfo.normalTexture);
        SamplerState normalSampler      = Bindless(SamplerState, materialInfo.normalSampler);

        float3 xyz;
        xyz.xy = normalTexture.SampleGrad(normalSampler, meshUv, meshUv_ddx, meshUv_ddy) * 2.0 - 1.0; // Remap to [-1, 1].
        xyz.z  = sqrt(1.0 - dot(xyz.xy, xyz.xy)); // Construct z.

        // Apply normal scale.
        xyz.xy *= materialInfo.normalFactorScale;

        // Tangent space to world sapce.
        normalRS = mul(normalize(xyz), TBN);
    }
    else
    {
        normalRS = vertNormalRS;
    }
    
    return baseColor.xyz * dot(normalRS, -scene.sunInfo.direction);// ;
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

    // Exist normal texture or not.
    bool bExistNormalTexture;

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

            // Update state. 
            bExistNormalTexture = (materialInfo.normalTexture != kUnvalidIdUint32);

            // Load triangle infos.
            const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
            const float4x4 translatedWorldToClip  = perView.translatedWorldToClip;
            const float4x4 localToClip            = mul(translatedWorldToClip, localToTranslatedWorld);
            uint triangleIndexId;
            getTriangleMiscInfo(perView, scene, objectInfo, bExistNormalTexture, localToClip, meshletId, triangleId, triangleInfo, triangleIndexId);

            // Update cache pack id.
            cachePackId = packID;
        }

        //
    #if LIGHTING_TYPE == kLightingType_GLTF_MetallicRoughnessPBR
        [branch]
        if (packID != 0 && materialInfo.materialType == kLightingType_GLTF_MetallicRoughnessPBR)
        {
            float3 c = gltfMetallicRoughnessPBR(perView, scene, objectInfo, materialInfo, triangleInfo, bExistNormalTexture, dispatchPos);
            storeColor(dispatchPos, c);
        }
    #endif 
    }
}


#endif // !__cplusplus