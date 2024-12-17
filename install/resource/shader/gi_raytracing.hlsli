#pragma once

#include "lighting.hlsli"

/*********************************
pushConsts
{
    uint cascadeCount;
    uint shadowViewId;
    uint shadowDepthIds;
    uint transmittanceId;

    uint scatteringId;
    uint singleMieScatteringId;
    uint linearSampler;
    uint irradianceTextureId;

    float rayMissDistance;
    float maxRayTraceDistance;
    float rayHitLODOffset;
    uint  bHistoryValid;

    uint  clipmapConfigBufferId;
    uint  clipmapCount;
    float skyLightLeaking;
    uint  bSampleWorldCache;
};
***********************************/

float4 rayTrace(in const PerframeCameraView perView, float3 rayOrigin, float3 rayDirection, float deviceZ, int sampleWeightMode)
{
    const GPUBasicData scene = perView.basicData;

    //
    float rayStart = getRayTraceStartOffset(perView.zNear, deviceZ);

    const uint traceFlag = RAY_FLAG_CULL_NON_OPAQUE;

    GIRayQuery query;
    RayDesc ray = getRayDesc(rayOrigin, rayDirection, rayStart, pushConsts.maxRayTraceDistance);
    query.TraceRayInline(topLevelAS, traceFlag, 0xFF, ray);
    query.Proceed(); 

    uint singleMieScatteringId = (scene.atmosphere.bCombineScattering == 0) ? pushConsts.singleMieScatteringId : pushConsts.scatteringId;
    Texture2D<float4> transmittanceTexture       = TBindless(Texture2D, float4, pushConsts.transmittanceId);
    Texture2D<float4> irradianceTexture          = TBindless(Texture2D, float4, pushConsts.irradianceTextureId);
    Texture3D<float4> scatteringTexture          = TBindless(Texture3D, float4, pushConsts.scatteringId);
    Texture3D<float4> singleMieScatteringTexture = TBindless(Texture3D, float4, singleMieScatteringId);



    float hitT;
    float3 radiance = 0.0;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        RayHitMaterialInfo materialInfo;
        materialInfo.init(scene, query, pushConsts.rayHitLODOffset);

        hitT = materialInfo.hitT;
        [branch] 
        if (materialInfo.isValidHit())
        {
            float3 positionRS = rayOrigin + hitT * rayDirection;

            PBRMaterial pbrMaterial;
            pbrMaterial.initFromRayHitMaterialInfo(materialInfo);

            //
            float3 V = -normalize(positionRS);
            float NoV = max(0.0, dot(materialInfo.normalRS, V));
            float2 brdf = sampleBRDFLut(perView, NoV, materialInfo.roughness); 

            //
            float3 approxDiffuseFullRough = materialInfo.diffuseColor + pbrMaterial.specularColor * 0.45;

            // Apply sun light direct diffuse lighting. 
            float3 skyRadiance;
            {
                float3 positionWS_km = float3(double3(positionRS / 1000.0f) + perView.cameraPositionWS_km.getDouble3());

                float3 skyIrradiance;
                float3 sunIrradiance = GetSunAndSkyIrradiance(
                    scene.atmosphere,     //  
                    transmittanceTexture, //
                    irradianceTexture,    //
                    positionWS_km - scene.atmosphere.earthCenterKm, // Get atmosphere unit position.
                    materialInfo.normalRS,                 // 
                    -scene.sunInfo.direction, //   
                    skyIrradiance); 

                // 
                float3 sunRadiance = finalRadianceExposureModify(scene, sunIrradiance + skyIrradiance);
                skyRadiance = finalRadianceExposureModify(scene, skyIrradiance);

                float NoL = max(0.0, dot(materialInfo.normalRS, -scene.sunInfo.direction));
                if (NoL > 0.0)
                {
                    CascadeShadowInfo cascadeInfo;
                    cascadeInfo.cacasdeCount           = pushConsts.cascadeCount;
                    cascadeInfo.shadowViewId           = pushConsts.shadowViewId;
                    cascadeInfo.shadowPaddingTexelSize = 0.0;   // Probe don't do any filter so don't need padding texel size.
                    cascadeInfo.positionRS             = positionRS;
                    cascadeInfo.zBias                  = 1e-4f; // This small z bias is fine for most case. 

                    float visibility = 1.0;
                    {
                        uint selectedCascadeId;
                        float3 shadowCoord = fastCascadeSelected(cascadeInfo, perView, selectedCascadeId);
                        if (all(shadowCoord >= 0.0))
                        { 
                            const CascadeShadowDepthIds shadowDepthIds = BATL(CascadeShadowDepthIds, pushConsts.shadowDepthIds, 0);
                            visibility *= cascadeShadowProjection(shadowCoord, selectedCascadeId, perView, shadowDepthIds);
                        }
                        else
                        {
                            // NOTE: Ray trace scene to get ray shadow value without ray sort is slow. 
                            //       We already combine cache cascade shadow map and sdsm, whole scene should cover by cascade shadow map. 
                        }
                    }

                    float3 sunDirectRadiance = 0.0;
                #if 1
                    // GGX specular and lambert diffuse.
                    ShadingResult sunShadingResult = evaluateDirectionalDirectLight_LightingType_GLTF_MetallicRoughnessPBR(
                        scene.sunInfo.direction, sunRadiance, pbrMaterial, materialInfo.normalRS, normalize(positionRS));
                    sunDirectRadiance = sunShadingResult.color();
                #else
                    // Full lambert diffuse. 
                    sunDirectRadiance = NoL * Fd_LambertDiffuse(approxDiffuseFullRough); 
                #endif 

                    // Do lambert diffuse lighting here.
                    radiance += sunDirectRadiance * visibility; 
                } 
            }



            // Infinite indirect lighting. 
            float sampleIndirectVolumeSampleCount = kGIMaxSampleCount;
            if (pushConsts.bHistoryValid && !perView.bCameraCut && pushConsts.bSampleWorldCache)
            {
                float3 irradiance = 0.0; 
                float3 specularIrradiance = 0.0;

                float3 reflectDir = reflect(-V, materialInfo.normalRS);
                for (uint cascadeId = 0; cascadeId < pushConsts.clipmapCount; cascadeId ++) 
                { 
                    const GIWorldProbeVolumeConfig config = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, cascadeId);
                    if (config.getBlendWeight(positionRS) > 0.99 && !config.bResetAll)
                    {
                        float minSampleCount;
                        if (config.evaluateSH(perView, positionRS, materialInfo.normalRS, irradiance, minSampleCount, sampleWeightMode))
                        {
                            config.evaluateSH(perView, positionRS, reflectDir, specularIrradiance, minSampleCount, sampleWeightMode);

                            // Check volume sample count, which used for ray trace. 
                            sampleIndirectVolumeSampleCount = min(sampleIndirectVolumeSampleCount, minSampleCount);
                            break; 
                        }
                        else
                        {
                            irradiance = 0.0; //  
                        } 
                    }  
                }   
                //
                radiance += irradiance * materialInfo.diffuseColor;
                radiance += specularIrradiance * (pbrMaterial.specularColor * brdf.x + brdf.y);  
            }

            // Sky light leaking for diffuse, don't add for reflection.
            radiance +=  pushConsts.skyLightLeaking * skyRadiance * materialInfo.diffuseColor;
            radiance +=  pushConsts.skyLightLeaking * skyRadiance * (pbrMaterial.specularColor * brdf.x + brdf.y);  

            // Emissive. 
            radiance += materialInfo.emissiveColor;
        }
    }
    else 
    {
        const float3 sunDirection = -scene.sunInfo.direction;
        const float3 worldDirectionRS = rayDirection;

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
        radiance = GetSkyRadiance(
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
        radiance = finalRadianceExposureModify(scene, radiance);

        // Far distance hit. 
        hitT = pushConsts.rayMissDistance; // Small offset as sky hit. 
    }

    return float4(radiance, hitT);
}
