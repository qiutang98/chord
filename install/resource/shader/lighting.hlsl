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
    uint transmittanceId;
    uint scatteringId;
    uint singleMieScatteringId;

    uint irradianceTextureId;
    uint linearSampler;
    uint baseColorId;
};
CHORD_PUSHCONST(LightingPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

// Atmosphere shared.
#define ATMOSPHERE_LINEAR_SAMPLER Bindless(SamplerState, pushConsts.linearSampler)
#define DISABLE_ATMOSPHERE_CHECK
#include "atmosphere.hlsli"

#include "nanite_shared.hlsli"
#include "colorspace.h"
#include "material.hlsli"
#include "lighting.hlsli"

#define DEBUG_CHECK_2x2_TILE 0

void exportGbuffer(in const TinyGBufferContext g, uint2 id)
{
    float3 finalColor = g.color;
    if (any(isnan(finalColor)))
    {
        finalColor = 0.0;
    }

    // 
    storeRWTexture2D_float3(pushConsts.sceneColorId,          id, finalColor);

#if LIGHTING_TYPE != kLightingType_None
    storeRWTexture2D_float3(pushConsts.baseColorId,           id, float3(g.baseColor.xyz));
    storeRWTexture2D_float4(pushConsts.vertexNormalRSId,      id, float4(g.vertexNormalRS * 0.5 + 0.5, 1.0f));
    storeRWTexture2D_float4(pushConsts.pixelNormalRSId,       id, float4(g.pixelNormalRS  * 0.5 + 0.5, 1.0f));
    storeRWTexture2D_float2(pushConsts.motionVectorId,        id, g.motionVector);
    storeRWTexture2D_float4(pushConsts.aoRoughnessMetallicId, id, float4(g.materialAO, g.roughness, g.metallic, 0.0));
#endif
}

float3 skyColorEvaluate(float2 uv, in const PerframeCameraView perView, in const GPUBasicData scene)
{
    float3 worldDirectionRS;
    {
        const float4 viewPointCS = float4(screenUvToNdcUv(uv), kFarPlaneZ, 1.0);
        const float4 viewPointRS = mul(perView.clipToTranslatedWorld, viewPointCS);
        worldDirectionRS = normalize(viewPointRS.xyz / viewPointRS.w);
    }

    uint singleMieScatteringId = (scene.atmosphere.bCombineScattering == 0) ? pushConsts.singleMieScatteringId : pushConsts.scatteringId;
    Texture2D<float4> transmittanceTexture       = TBindless(Texture2D, float4, pushConsts.transmittanceId);
    Texture2D<float4> irradianceTexture          = TBindless(Texture2D, float4, pushConsts.irradianceTextureId);
    Texture3D<float4> scatteringTexture          = TBindless(Texture3D, float4, pushConsts.scatteringId);
    Texture3D<float4> singleMieScatteringTexture = TBindless(Texture3D, float4, singleMieScatteringId);

    const float3 sunDirection = -scene.sunInfo.direction;

    // Compute the radiance reflected by the ground, if the ray intersects it.
    float groundAlpha = 0.0;
    float3 groundRadiance = 0.0;

    // Can hack to float here when unit is kilometers.
    float3 cameraToEarthCenterKm = perView.cameraToEarthCenter_km.castFloat3();
    float3 cameraPositionWS_km = perView.cameraPositionWS_km.castFloat3();
    {
        float3 p = cameraToEarthCenterKm; 

        // 
        float pov = dot(p, worldDirectionRS);
        float pop = dot(p, p);

        float rayEarthCenterSquaredDistance = pop - pov * pov;
        float distance2intersection = -pov - sqrt(scene.atmosphere.bottom_radius * scene.atmosphere.bottom_radius - rayEarthCenterSquaredDistance);
        if (distance2intersection > 0.0)
        {
            float3 intersectPoint = cameraPositionWS_km + worldDirectionRS * distance2intersection;
            float3 normal = normalize(intersectPoint - scene.atmosphere.earthCenterKm);

            // Compute the radiance reflected by the ground.
            float3 skyIrradiance;
            float3 sunIrradiance = GetSunAndSkyIrradiance(
                scene.atmosphere, 
                transmittanceTexture,
                irradianceTexture, 
                intersectPoint - scene.atmosphere.earthCenterKm, 
                normal, 
                sunDirection,
                skyIrradiance);

            float sunVis = 1.0f; 
            float skyVis = 1.0f; 

            // Lambert lighting diffuse model.
            groundRadiance = scene.atmosphere.ground_albedo * (1.0 / kPI) * (sunIrradiance * sunVis + skyIrradiance * skyVis);

        #if 0
            // Composite transmittance here lose visibility info.
            // Air perspective finish on volumetric fog.
            float3 transmittance;
            float3 inScatter = GetSkyRadianceToPoint( 
                scene.atmosphere, 
                transmittanceTexture,
                scatteringTexture, 
                singleMieScatteringTexture,  
                cameraToEarthCenterKm,
                intersectPoint - scene.atmosphere.earthCenterKm, 
                sunDirection, 
                transmittance);

            groundRadiance = groundRadiance * transmittance + inScatter;
		#endif
            groundAlpha = 1.0;  
        } 
    }

    float3 transmittance;
    float3 radiance = GetSkyRadiance(
        scene.atmosphere,  
        transmittanceTexture,
        scatteringTexture, 
        singleMieScatteringTexture,  
        cameraToEarthCenterKm,
        worldDirectionRS, 
        sunDirection,  
        transmittance); 

    // 
    radiance = lerp(radiance, groundRadiance, groundAlpha);

    //   
    return finalRadianceExposureModify(scene, radiance);
}

void gltfMetallicRoughnessPBR_Lighting(
    in const PerframeCameraView perView,
    in const GPUBasicData scene,
    inout TinyGBufferContext g)
{
    PBRMaterial material;
    material.initGltfMetallicRoughnessPBR(g);

    ShadingResult shadingResult;
    shadingResult.init();

    const float3 pixel2CamDirectionRS = normalize(g.positionRS);

    // Sun  
    {
        float3 cameraToEarthCenterKm = perView.cameraToEarthCenter_km.castFloat3();

        uint singleMieScatteringId = (scene.atmosphere.bCombineScattering == 0) ? pushConsts.singleMieScatteringId : pushConsts.scatteringId;
        Texture2D<float4> transmittanceTexture       = TBindless(Texture2D, float4, pushConsts.transmittanceId);
        Texture2D<float4> irradianceTexture          = TBindless(Texture2D, float4, pushConsts.irradianceTextureId);
        Texture3D<float4> scatteringTexture          = TBindless(Texture3D, float4, pushConsts.scatteringId);
        Texture3D<float4> singleMieScatteringTexture = TBindless(Texture3D, float4, singleMieScatteringId);

        // 
        float3 positionWS_km = float3(double3(g.positionRS / 1000.0f) + perView.cameraPositionWS_km.getDouble3());

        float3 skyIrradiance;
        float3 sunIrradiance = GetSunAndSkyIrradiance(
            scene.atmosphere,     // 
            transmittanceTexture, //
            irradianceTexture,    //
            positionWS_km - scene.atmosphere.earthCenterKm, // Get atmosphere unit position.
            g.pixelNormalRS,      //
            -scene.sunInfo.direction, // 
            skyIrradiance);

        ShadingResult sunShadingResult = evaluateDirectionalDirectLight_LightingType_GLTF_MetallicRoughnessPBR(
            scene.sunInfo.direction, sunIrradiance + skyIrradiance, material, g.pixelNormalRS, pixel2CamDirectionRS);

    #if 0
        // Composite transmittance here lose visibility info.
        // Air perspective finish on volumetric fog. 
        float3 transmittance;
        float3 inScatter = GetSkyRadianceToPoint( 
            scene.atmosphere, 
            transmittanceTexture,
            scatteringTexture, 
            singleMieScatteringTexture,  
            cameraToEarthCenterKm,
            positionWS_km - scene.atmosphere.earthCenterKm, 
            -scene.sunInfo.direction, // 
            transmittance);

        sunShadingResult.diffuseTerm = sunShadingResult.diffuseTerm * transmittance + inScatter;
        sunShadingResult.specularTerm = sunShadingResult.specularTerm * transmittance + inScatter;
    #endif

        sunShadingResult.diffuseTerm = finalRadianceExposureModify(scene, sunShadingResult.diffuseTerm);
        sunShadingResult.specularTerm = finalRadianceExposureModify(scene, sunShadingResult.specularTerm);

        shadingResult.composite(sunShadingResult);
    } 

    // 
    g.color += shadingResult.color();
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

    #if LIGHTING_TYPE != kLightingType_None
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
    #endif

    #if LIGHTING_TYPE == kLightingType_None
        [branch]
        if (packID == 0)
        {
            gbufferCtx.color = skyColorEvaluate(screenUV, perView, scene);

            // 
            exportGbuffer(gbufferCtx, dispatchPos);
        }     
    #elif LIGHTING_TYPE == kLightingType_GLTF_MetallicRoughnessPBR
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