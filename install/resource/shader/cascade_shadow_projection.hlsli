#pragma once

#include "bindless.hlsli"

// Depth Aware Contact harden pcf. See GDC2021: "Shadows of Cold War" for tech detail.
// Use cache occluder dist to fit one curve similar to tonemapper, to get some effect like pcss.
// can reduce tiny acne natively.
float contactHardenPCFKernal(const float occluders, const float occluderDistSum, const float compareDepth, const uint shadowSampleCount)
{
    // Normalize occluder dist.
    float occluderAvgDist = occluderDistSum / occluders;
    float w = 1.0f / float(shadowSampleCount); 
    
    // 0 -> contact harden.
    // 1 -> soft, full pcf.
    float pcfWeight =  clamp(occluderAvgDist / compareDepth, 0.0, 1.0);
    
    // Normalize occluders.
    float percentageOccluded = clamp(occluders * w, 0.0, 1.0);

    // S curve fit.
    percentageOccluded = 2.0f * percentageOccluded - 1.0f;
    float occludedSign = sign(percentageOccluded);
    percentageOccluded = 1.0f - (occludedSign * percentageOccluded);
    percentageOccluded = lerp(percentageOccluded * percentageOccluded * percentageOccluded, percentageOccluded, pcfWeight);
    percentageOccluded = 1.0f - percentageOccluded;

    percentageOccluded *= occludedSign;
    percentageOccluded = 0.5f * percentageOccluded + 0.5f;

    return 1.0f - percentageOccluded;
}

// Computes the receiver plane depth bias for the given shadow coord in screen space.
// http://mynameismjp.wordpress.com/2013/09/10/shadow-maps/ 
// http://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2012/10/Isidoro-ShadowMapping.pdf
float2 computeReceiverPlaneDepthBias(float3 texCoordDX, float3 texCoordDY)
{
    float2 biasUV;

    biasUV.x = texCoordDY.y * texCoordDX.z - texCoordDX.y * texCoordDY.z;
    biasUV.y = texCoordDX.x * texCoordDY.z - texCoordDY.x * texCoordDX.z;

    // 
    float r = texCoordDX.x * texCoordDY.y - texCoordDX.y * texCoordDY.x;
    return biasUV / r;
}

struct CascadeShadowInfo
{
    uint cacasdeCount;
    uint shadowViewId;

    //
    float shadowPaddingTexelSize; // Texel pad for selected.
    float3 positionRS;

    // 
    float zBias;
};

float3 fastCascadeSelected(
    in const CascadeShadowInfo info, 
    in const PerframeCameraView perView,
    out uint selectedCascadeId)
{
    uint activeCascadeId = 0;
    float3 shadowCoord;

    //
    float cascadeRadiusScale;
    float cascdeSplit;
    for (uint cascadeId = 0; cascadeId < info.cacasdeCount; cascadeId ++)
    {
        const InstanceCullingViewInfo shadowView = BATL(InstanceCullingViewInfo, info.shadowViewId, cascadeId);
        const float3 positionRS_cacsade = info.positionRS + float3(perView.cameraWorldPos.getDouble3() - shadowView.cameraWorldPos.getDouble3());

        shadowCoord = mul(shadowView.translatedWorldToClip, float4(positionRS_cacsade, 1.0)).xyz;

        // Remap to [0, 1]
        shadowCoord.xy = shadowCoord.xy * float2(0.5, -0.5) + 0.5;

        cascdeSplit = shadowView.orthoDepthConvertToView.z;
        cascadeRadiusScale = shadowView.orthoDepthConvertToView.w;

        if (all(shadowCoord.xy > info.shadowPaddingTexelSize) &&
            all(shadowCoord.xy < 1.0 - info.shadowPaddingTexelSize) && 
            shadowCoord.z > 0.0 && 
            shadowCoord.z < 1.0)
        {
            break;
        }

        activeCascadeId ++;
    }

    // 
    selectedCascadeId = activeCascadeId;

    if (activeCascadeId >= info.cacasdeCount)
    {
        shadowCoord = -1.0;
    }
    else
    {
        // Apply z offset.
        shadowCoord.z += info.zBias / cascadeRadiusScale * cascdeSplit;
    }

    return shadowCoord;
}

float cascadeShadowProjection(
    in float3 shadowCoord,
    in uint activeCascadeId,
    in const PerframeCameraView perView,
    in const CascadeShadowDepthIds shadowDepthIds)
{
    //
    SamplerState pointClampSampler = getPointClampEdgeSampler(perView);
    Texture2D<float> shadowDepthTexture = TBindless(Texture2D, float, shadowDepthIds.shadowDepth[activeCascadeId]);

    return shadowDepthTexture.SampleLevel(pointClampSampler, shadowCoord.xy, 0) < shadowCoord.z;
}

float cascadeShadowProjectionx4(
    in float3 shadowCoord,
    in uint activeCascadeId,
    in const PerframeCameraView perView,
    in const CascadeShadowDepthIds shadowDepthIds)
{
    //
    SamplerState pointClampSampler = getPointClampEdgeSampler(perView);

    //
    Texture2D<float> shadowDepthTexture = TBindless(Texture2D, float, shadowDepthIds.shadowDepth[activeCascadeId]);
    float4 depthShadow4 = shadowDepthTexture.Gather(pointClampSampler, shadowCoord.xy);

    float occluders = 0.0f;
    float occluderDistSum = 0.0f;
    [unroll(4)]
    for (uint i = 0; i < 4; i ++)
    {
        // 
        float dist = depthShadow4[i] - shadowCoord.z;
        float occluder = step(0.0, dist); // reverse z.

        // Collect occluders.
        occluders += occluder;
        occluderDistSum += dist * occluder;
    }

    return contactHardenPCFKernal(occluders, occluderDistSum, shadowCoord.z, 4);
}