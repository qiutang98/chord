#include "ddgi.h"

struct DDGIRelightingPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(DDGIRelightingPushConsts);

    uint cameraViewId;
    uint ddgiConfigBufferId;
    uint ddgiCount;
    uint radianceUAV; 

    uint cascadeCount;
    uint shadowViewId;
    uint shadowDepthIds;
    uint transmittanceId;

    uint scatteringId;
    uint singleMieScatteringId;
    uint linearSampler;
    uint irradianceTextureId;

    uint ddgiConfigId;
    uint probeTracedMarkSRV;
    uint probeCacheInfoSRV;
    uint probeCacheRayGbufferSRV;

    uint probeTraceLinearIndexSRV;
};
CHORD_PUSHCONST(DDGIRelightingPushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli" 
#include "bindless.hlsli"
#include "gltf.h"
#include "cascade_shadow_projection.hlsli"
#include "colorspace.h" 
#include "debug.hlsli"
#include "debug_line.hlsli"

// Atmosphere shared.
#define ATMOSPHERE_LINEAR_SAMPLER Bindless(SamplerState, pushConsts.linearSampler)
#define DISABLE_ATMOSPHERE_CHECK
#include "atmosphere.hlsli"

// Update every frame. 
[numthreads(kDDGITraceThreadCount, 1, 1)]
void mainCS(    
    uint2 workGroupId     : SV_GroupID, 
    uint localThreadIndex : SV_GroupIndex,
    uint dispatchId       : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // 
    const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, pushConsts.ddgiConfigId);

    // Now get ray index. 
    const uint linearProbeIndex = BATL(uint, pushConsts.probeTraceLinearIndexSRV, workGroupId.x);
    const int rayIndex = localThreadIndex + workGroupId.y * kDDGITraceThreadCount;
 
    // Early return if current ray index out of range. 
    if (rayIndex >= kDDGIPerProbeRayCount)
    {  
        return;  
    }

    const int3 virtualProbeIndex = ddgiConfig.getVirtualVolumeId(linearProbeIndex);  
    const int  physicsProbeLinearIndex = ddgiConfig.getPhysicalLinearVolumeId(virtualProbeIndex);

    const int probeState = BATL(int, pushConsts.probeTracedMarkSRV, physicsProbeLinearIndex);
    if (probeState < 0)
    {
        // No ready traced result so return. 
        return; 
    }

    DDGIProbeCacheTraceInfo probeCacheTraceInfo = BATL(DDGIProbeCacheTraceInfo, pushConsts.probeCacheInfoSRV, physicsProbeLinearIndex);
    float3 probePositionRS = float3(probeCacheTraceInfo.worldPosition.getDouble3() - perView.cameraWorldPos.getDouble3());
    float3 rayDirection = ddgiConfig.getSampleRayDir(rayIndex);
    rayDirection = mul(probeCacheTraceInfo.rayRotation, float4(rayDirection, 0.0)).xyz;

    // Final result.
    float3 radiance = 0.0;

 
    //  
    int raytStorePosition = physicsProbeLinearIndex * kDDGIPerProbeRayCount + rayIndex;
    DDGIProbeCacheMiniGbuffer miniGbuffer = BATL(DDGIProbeCacheMiniGbuffer, pushConsts.probeCacheRayGbufferSRV, raytStorePosition);

    // 
    uint singleMieScatteringId = (scene.atmosphere.bCombineScattering == 0) ? pushConsts.singleMieScatteringId : pushConsts.scatteringId;
    Texture2D<float4> transmittanceTexture       = TBindless(Texture2D, float4, pushConsts.transmittanceId);
    Texture2D<float4> irradianceTexture          = TBindless(Texture2D, float4, pushConsts.irradianceTextureId);
    Texture3D<float4> scatteringTexture          = TBindless(Texture3D, float4, pushConsts.scatteringId);
    Texture3D<float4> singleMieScatteringTexture = TBindless(Texture3D, float4, singleMieScatteringId);

    // Sky hit. 
    [branch]
    if (miniGbuffer.distance > ddgiConfig.rayTraceMaxDistance)
    {
        // Sky sample.
        float3 transmittance;
        radiance = GetSkyRadiance(
            scene.atmosphere,  
            transmittanceTexture,
            scatteringTexture, 
            singleMieScatteringTexture,  
            perView.cameraToEarthCenter_km.castFloat3(),
            rayDirection, 
            -scene.sunInfo.direction,  
            transmittance); 

        radiance = finalRadianceExposureModify(scene, radiance);
    }
    else if (miniGbuffer.isRayHitBackface())
    {
        radiance = 0.0;
    }
    else
    {
        float3 diffuseColor = getDiffuseColor(miniGbuffer.baseColor, miniGbuffer.metallic);
        float3 normalRS     = miniGbuffer.normalRS;
        float3 positionRS   = probePositionRS + rayDirection * miniGbuffer.distance;

        // Apply sun light direct diffuse lighting. 
        {
            float NoL = max(0.0, dot(normalRS, -scene.sunInfo.direction));
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

                float3 positionWS_km = float3(double3(positionRS / 1000.0f) + perView.cameraPositionWS_km.getDouble3());
 
                float3 skyIrradiance;
                float3 sunIrradiance = GetSunAndSkyIrradiance(
                    scene.atmosphere,     // 
                    transmittanceTexture, //
                    irradianceTexture,    //
                    positionWS_km - scene.atmosphere.earthCenterKm, // Get atmosphere unit position.
                    normalRS,                 //
                    -scene.sunInfo.direction, // 
                    skyIrradiance);

                // 
                float3 sunRadiance = finalRadianceExposureModify(scene, skyIrradiance + sunIrradiance);

                // Do lambert diffuse lighting here.
                radiance += sunRadiance * visibility * NoL * Fd_LambertDiffuse(diffuseColor);
            }
        }

        // Apply history ddgi indirect diffuse. 
        if (ddgiConfig.bHistoryValid)
        {
            float weightSum = 0.0;
            float3 irradianceWeights = 0.0;
            
            for (uint ddgiIndex = 0; ddgiIndex < pushConsts.ddgiCount; ddgiIndex ++)
            {
                DDGIVoulmeConfig iterDdgiConfig;
                if (ddgiIndex == pushConsts.ddgiConfigId)
                { 
                    iterDdgiConfig = ddgiConfig;
                }
                else
                {
                    iterDdgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, ddgiIndex);
                }

                float weight = iterDdgiConfig.getBlendWeight(positionRS);
                if (weightSum >= 1.0)
                {
                    break; 
                }

                [branch]
                if (weight <= 0.0)
                {
                    continue;
                }
                else 
                {
                    float3 surfaceBias =  iterDdgiConfig.getSurfaceBias(normalRS, rayDirection);
                    float3 radiance = iterDdgiConfig.sampleIrradiance(perView, positionRS, surfaceBias, normalRS);

                    // 
                    float linearWeight = (weightSum > 0.0) ? (1.0 - weightSum) : weight;

                    // 
                    irradianceWeights += radiance * linearWeight;
                    weightSum += linearWeight; 
                }
            }  

            float3 irradiance = 0.0; 
            if (weightSum > 0.0)
            {  
                irradiance = irradianceWeights / weightSum; 
            } 

            // Perfectly diffuse reflectors don't exist in the real world. Limit the BRDF
            // albedo to a maximum value to account for the energy loss at each bounce.
            float maxDiffuseColor = 0.9;

            // Shading.
            radiance += irradiance * kInvertPI * min(diffuseColor, maxDiffuseColor);
        }
    }

    // Store radiance result.
    BATS(float3, pushConsts.radianceUAV, raytStorePosition, radiance);
}

#endif //!__cplusplus