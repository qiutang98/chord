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

    uint vertexNormalRSId;   
    uint pixelNormalRSId;  
    uint motionVectorId;   
    uint aoRoughnessMetallicId; 

    uint drawedMeshletCmdId;
};
CHORD_PUSHCONST(LightingPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "nanite_shared.hlsli"
#include "colorspace.h"
#include "material.hlsli"

#define DEBUG_CHECK_2x2_TILE 0

void exportGbuffer(in const TinyGBufferContext g, uint2 id)
{
    storeRWTexture2D_float3(pushConsts.sceneColorId,          id, g.color);
    storeRWTexture2D_float4(pushConsts.vertexNormalRSId,      id, float4(g.vertexNormalRS * 0.5 + 0.5, 1.0f));
    storeRWTexture2D_float4(pushConsts.pixelNormalRSId,       id, float4(g.pixelNormalRS  * 0.5 + 0.5, 1.0f));
    storeRWTexture2D_float2(pushConsts.motionVectorId,        id, g.motionVector);
    storeRWTexture2D_float4(pushConsts.aoRoughnessMetallicId, id, float4(g.materialAO, g.roughness, g.metallic, 0.0));
}

void gltfMetallicRoughnessPBR_Lighting(
    in const PerframeCameraView perView,
    in const GPUBasicData scene,
    inout TinyGBufferContext gbufferCtx)
{
    // Motion vector. 
    gbufferCtx.color = gbufferCtx.baseColor.xyz * dot(gbufferCtx.pixelNormalRS, -scene.sunInfo.direction);// ;
}

[numthreads(64, 1, 1)]
void mainCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    //
    Texture2D<uint> visibilityTexture  = TBindless(Texture2D, uint,  pushConsts.visibilityId);

    //
    uint cachePackId = 0;

    // Cache data.  
    GPUObjectGLTFPrimitive objectInfo;
    GLTFMaterialGPUData    materialInfo;
    TriangleMiscInfo       triangleInfo;

    const uint  sampleGroup = workGroupId * 4 + localThreadIndex / 16;
    const uint2 tileOffset  = BATL(uint2, pushConsts.tileBufferCmdId, sampleGroup);

    // 
    TinyGBufferContext gbufferCtx;


#if DEBUG_CHECK_2x2_TILE
    // One lane handle 2x2 tile continually.
    uint2 halfStorePos = 0;
#endif

    [unroll(4)]
    for (uint i = 0; i < 4; i ++)
    {
        const uint2 dispatchPos = tileOffset + remap8x8((localThreadIndex % 16) * 4 + i);
        const float2 screenUV = (dispatchPos + 0.5) * pushConsts.visibilityTexelSize;

    #if DEBUG_CHECK_2x2_TILE
        // Continually debugging. 
        if (i == 0) 
        { 
            halfStorePos = dispatchPos / 2; 
        }
        else 
        { 
            check(all(halfStorePos == (dispatchPos / 2))); 
        }
    #endif

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
            const bool bExistNormalTexture = (materialInfo.normalTexture != kUnvalidIdUint32);

            // Load triangle infos.
            const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
            const float4x4 translatedWorldToClip  = perView.translatedWorldToClip;
            const float4x4 localToClip            = mul(translatedWorldToClip, localToTranslatedWorld);

            const float4x4 localToClip_NoJitter            = mul(perView.translatedWorldToClip_NoJitter, localToTranslatedWorld);
            const float4x4 localToClip_LastFrame_NoJitter  = mul(perView.translatedWorldToClipLastFrame_NoJitter, objectInfo.basicData.localToTranslatedWorldLastFrame);

            triangleInfo = getTriangleMiscInfo(scene, objectInfo, bExistNormalTexture, localToClip, localToClip_NoJitter, localToClip_LastFrame_NoJitter, meshletId, triangleId);

            // Update cache pack id.
            cachePackId = packID;
        }

        //
    #if LIGHTING_TYPE == kLightingType_GLTF_MetallicRoughnessPBR
        [branch]
        if (packID != 0 && materialInfo.materialType == kLightingType_GLTF_MetallicRoughnessPBR)
        {
            loadGLTFMetallicRoughnessPBRMaterial(materialInfo, triangleInfo, dispatchPos, pushConsts.visibilityTexelSize, gbufferCtx);

            // Do directional light lighting.
            gltfMetallicRoughnessPBR_Lighting(perView, scene, gbufferCtx);

            // 
            exportGbuffer(gbufferCtx, dispatchPos);
        }
    #endif 
    }
}


#endif // !__cplusplus