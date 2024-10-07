#include "base.h"

struct SkyPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(SkyPushConsts);

    uint cameraViewId;
    uint depthId;
    uint linearSampler;
    uint sceneColorId;

    uint transmittanceId;
    uint scatteringId;
    uint singleMieScatteringId;
    uint irradianceTextureId;
};
CHORD_PUSHCONST(SkyPushConsts, pushConsts);

// Atmosphere shared.
#define ATMOSPHERE_LINEAR_SAMPLER Bindless(SamplerState, pushConsts.linearSampler)
#define DISABLE_ATMOSPHERE_CHECK
#include "atmosphere.hlsli"

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void skyRenderCS(uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex); 
    if (any(workPos >= uint2(perView.renderDimension.xy)))
    {
        return;
    }

    Texture2D<float> depthTexture = TBindless(Texture2D, float, pushConsts.depthId);
    float deviceZ = depthTexture[workPos];

    if (deviceZ > 0.0)
    {
        return;   
    }

    const float2 fragCoord = workPos + 0.5;
    const float2 uv = fragCoord * perView.renderDimension.zw;

    float3 worldDirectionRS;
    {
        const float4 viewPointCS = float4(screenUvToNdcUv(uv), kFarPlaneZ, 1.0);
        float4 viewPointRS = mul(perView.clipToTranslatedWorld, viewPointCS);
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
    float3 cameraToEarthCenterKm = float3(asDouble3(perView.cameraToEarthCenter_km));
    float3 cameraPositionWS_km = float3(asDouble3(perView.cameraPositionWS_km));
    {
        float3 p = cameraToEarthCenterKm; 

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

            float sunVis = 1.0f; // TODO: sky shadow map. 
            float skyVis = 1.0f; // TODO: Sky ambient occlusion.

            // Lambert lighting diffuse model.
            groundRadiance = scene.atmosphere.ground_albedo * (1.0 / kPI) * (sunIrradiance * sunVis + skyIrradiance * skyVis);

            float3 transmittance;
            float3 inScatter = GetSkyRadianceToPoint( 
                scene.atmosphere, 
                transmittanceTexture,
                scatteringTexture, 
                singleMieScatteringTexture,  
                cameraToEarthCenterKm,
                intersectPoint - scene.atmosphere.earthCenterKm, 
                0.0,  
                sunDirection, 
                transmittance);

            groundRadiance = groundRadiance * transmittance + inScatter;
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
        0.0, 
        sunDirection,  
        transmittance); 

    radiance = lerp(radiance, groundRadiance, groundAlpha);
    
    RWTexture2D<float3> rwSceneColor = TBindless(RWTexture2D, float3, pushConsts.sceneColorId);
    rwSceneColor[workPos] = radiance * (scene.atmosphere.luminanceMode == 0 ? 1.0 : 1e-5f) * 10.0;
}
 
#endif