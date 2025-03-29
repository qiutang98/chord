#ifndef SHADER_ACCELERATE_STRUCTURE_VISUALIZE_HLSL
#define SHADER_ACCELERATE_STRUCTURE_VISUALIZE_HLSL

// Visualize pass for accelerate structure. 

#include "base.h"

struct AccelerationStructureVisualizePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(AccelerationStructureVisualizePushConsts);

    uint cameraViewId;
    uint uav;

    uint cascadeCount;
    uint shadowViewId;
    uint shadowDepthIds;

    uint transmittanceId;
    uint scatteringId;
    uint singleMieScatteringId;
    uint linearSampler;
};
CHORD_PUSHCONST(AccelerationStructureVisualizePushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli"
#include "bindless.hlsli"
#include "gltf.h"
#include "cascade_shadow_projection.hlsli"
#include "colorspace.h"


// Atmosphere shared.
#define ATMOSPHERE_LINEAR_SAMPLER Bindless(SamplerState, pushConsts.linearSampler)
#define DISABLE_ATMOSPHERE_CHECK
#include "atmosphere.hlsli"

[[vk::binding(0, 1)]] RaytracingAccelerationStructure topLevelAS;
#include "raytrace_shared.hlsli"

[numthreads(8, 8, 1)]
void mainCS(uint2 workPos : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint2 renderDim = uint2(perView.renderDimension.xy);
    if (any(workPos >= renderDim))
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

    float3 resultColor = 0.0;

    GIRayQuery query;
    RayDesc ray = getRayDesc(0, worldDirectionRS, 0.0, kDefaultRayQueryTMax);

    // 
    const uint traceFlag = RAY_FLAG_CULL_NON_OPAQUE;

    query.TraceRayInline(topLevelAS, traceFlag, 0xFF, ray);
    query.Proceed();
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        RayHitMaterialInfo materialInfo;
        materialInfo.init(scene, query);

        CascadeShadowInfo cascadeInfo;
        cascadeInfo.cacasdeCount           = pushConsts.cascadeCount;
        cascadeInfo.shadowViewId           = pushConsts.shadowViewId;
        cascadeInfo.shadowPaddingTexelSize = 0.0; // Current don't do any filter.
        cascadeInfo.positionRS             = materialInfo.hitT * worldDirectionRS;
        cascadeInfo.zBias                  = 1e-4f; // This small z bias is fine for most case. 

        float shadowValue = 1.0;
        {
            uint selectedCascadeId;
            float3 shadowCoord = fastCascadeSelected(cascadeInfo, perView, selectedCascadeId);
            if (all(shadowCoord >= 0.0))
            {
                const CascadeShadowDepthIds shadowDepthIds = BATL(CascadeShadowDepthIds, pushConsts.shadowDepthIds, 0);
                shadowValue = cascadeShadowProjection(shadowCoord, selectedCascadeId, perView, shadowDepthIds);
            }
        } 

        resultColor.xyz = shadowValue * max(0.0, dot(materialInfo.normalRS, -scene.sunInfo.direction)) * materialInfo.baseColor.xyz;
    }
    else
    {

        uint singleMieScatteringId = (scene.atmosphere.bCombineScattering == 0) ? pushConsts.singleMieScatteringId : pushConsts.scatteringId;
        Texture2D<float4> transmittanceTexture       = TBindless(Texture2D, float4, pushConsts.transmittanceId);
        Texture3D<float4> scatteringTexture          = TBindless(Texture3D, float4, pushConsts.scatteringId);
        Texture3D<float4> singleMieScatteringTexture = TBindless(Texture3D, float4, singleMieScatteringId);

        float3 transmittance;
        float3 radiance = GetSkyRadiance(
            scene.atmosphere,  
            transmittanceTexture,
            scatteringTexture, 
            singleMieScatteringTexture,  
            perView.cameraToEarthCenter_km.castFloat3(),
            worldDirectionRS, 
            -scene.sunInfo.direction,  
            transmittance); 

        resultColor.xyz = finalRadianceExposureModify(scene, radiance);
    }

    storeRWTexture2D_float3(pushConsts.uav, workPos, resultColor);
}

#endif // !__cplusplus

#endif // SHADER_ACCELERATE_STRUCTURE_VISUALIZE_HLSL