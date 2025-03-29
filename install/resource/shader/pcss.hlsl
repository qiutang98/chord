#ifndef SHADER_PCSS_HLSL
#define SHADER_PCSS_HLSL

#include "base.h"

struct ShadowProjectionPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(ShadowProjectionPushConsts);

    uint cameraViewId;
    uint cascadeCount;
    uint shadowViewId;
    uint depthId; 

    uint softShadowMaskTexture;
    uint uav;
    uint uav1;
    uint normalId;

    uint  bContactHardenPCF;             // false
    float shadowMapTexelSize;
    float normalOffsetScale;
    float lightSize;                     // 1.0

    float cascadeBorderJitterCount;      // 2.0
    float pcfBiasScale;                  // 20.0
    float biasLerpMin_const;             // 5.0
    float biasLerpMax_const;             // 10.0

    float3 lightDirection;
    uint shadowDepthIds;

    int blockerMinSampleCount;
    int blockerMaxSampleCount;
    int pcfMinSampleCount;
    int pcfMaxSampleCount;

    float blockerSearchMaxRangeScale;
    uint realtimeCascadeCount;
    uint motionVectorId;
    uint disocclusionMaskId;
};
CHORD_PUSHCONST(ShadowProjectionPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

// 

#include "bindless.hlsli" 
#include "debug.hlsli"
#include "base.hlsli"
#include "blue_noise.hlsli"
#include "sample.hlsli"
#include "cascade_shadow_projection.hlsli"

float pcssWidth(float cascadeRadiusScale)
{
    return pushConsts.lightSize * cascadeRadiusScale * 64.0 * pushConsts.shadowMapTexelSize; // * 200.0f;
}

// Surface normal based bias, see https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps for more details.
float3 biasNormalOffset(float NoL, float3 normal)
{
    return 2.0 * pushConsts.shadowMapTexelSize * pushConsts.normalOffsetScale * saturate(1.0f - NoL) * normal;
}

//
float getMinPercentage()
{
    return pushConsts.blockerSearchMaxRangeScale / pushConsts.lightSize;
}

// 
float2 pcssFindBlocker(
    in const CascadeShadowDepthIds shadowDepthIds,
    float pcfBias,
    float cascadeRadiusScale, 
    float3 shadowCoord, 
    SamplerState pointClampSampler, 
    uint activeCascadeId, 
    float angle)
{
    Texture2D<float> shadowDepthTexture = TBindless(Texture2D, float, shadowDepthIds.shadowDepth[activeCascadeId]);

    // 
    float blockerSum  = 0;
    float numBlockers = 0; 

    // 
    float searchWidth = getMinPercentage() * pcssWidth(cascadeRadiusScale); // min(pushConsts.shadowMapTexelSize * kPCSSBlockerSampleCount * 2.0, pcssWidth(cascadeRadiusScale));
    float searchWidthWeight = clamp(searchWidth / pushConsts.shadowMapTexelSize, 1.0, pushConsts.blockerMaxSampleCount);
    float lerpWeight = (searchWidthWeight - pushConsts.shadowMapTexelSize) / (pushConsts.blockerMaxSampleCount - 1.0);
    
    int sampleCount = int(lerp(float(pushConsts.blockerMinSampleCount), float(pushConsts.blockerMaxSampleCount), lerpWeight));

    // 
    for (int i = 0; i < sampleCount; ++i)
    {
        float2 sampleUv = shadowCoord.xy + vogelDiskSample(i, sampleCount, angle) * searchWidth;

        //  
        const float compareDepth = shadowCoord.z; // + pcfBias * pushConsts.pcfBiasScale; // + 1e-3f * max(abs(offset.x), abs(offset.y));

        // 
        float4 depthShadow4 = shadowDepthTexture.Gather(pointClampSampler, sampleUv);
        [unroll(4)]
        for (uint j = 0; j < 4; j ++)
        {
            // 
            float dist = depthShadow4[j] - compareDepth;
            float occluder = step(0.0, dist); // reverse z.

            numBlockers += occluder;
            blockerSum  += depthShadow4[j] * occluder;
        }
    }

    // 
    float avgBlockerDepth = blockerSum / numBlockers;
    return float2(avgBlockerDepth, numBlockers);
}

float shadowPCF(
    in const CascadeShadowDepthIds shadowDepthIds,
    float penumbraRatio,
    float pcfBias,
    float cascadeRadiusScale,
    float2 cascadeZconvertEye, 
    float3 shadowCoord, 
    SamplerState pointClampSampler, 
    uint activeCascadeId, 
    float angle)
{
    penumbraRatio = min(penumbraRatio, getMinPercentage());

    //
    float filterRadiusUV = penumbraRatio * pcssWidth(cascadeRadiusScale);
    filterRadiusUV = max(filterRadiusUV, pushConsts.shadowMapTexelSize);

    Texture2D<float> shadowDepthTexture = TBindless(Texture2D, float, shadowDepthIds.shadowDepth[activeCascadeId]);

    const float maxSampleRateDim = pushConsts.pcfMaxSampleCount; // Used to config sample rate, one pixel one width.
    float sampleTexelDim = filterRadiusUV / pushConsts.shadowMapTexelSize;

    float sampleRate = (min(sampleTexelDim, maxSampleRateDim + 1.0) - 1.0) / maxSampleRateDim;

    uint filterSampleCount = uint(lerp(float(pushConsts.pcfMinSampleCount), float(pushConsts.pcfMaxSampleCount), sampleRate)); 

    float occluders = 0.0f;
    float occluderDistSum = 0.0f;

    uint pixelCount = filterSampleCount * 4;
    for (uint i = 0; i < filterSampleCount; i++)
    {
        float2 randSample = vogelDiskSample(i, filterSampleCount, angle);
        float2 sampleUv = shadowCoord.xy + randSample * filterRadiusUV;

        float lengthRatio = penumbraRatio * length(randSample);

        // PCF bias scale don't care cascade radius, cancel it.
        const float biasPCF = lerp(0.0f, pcfBias * pushConsts.pcfBiasScale, lengthRatio);

        // 
        const float compareDepth = shadowCoord.z + biasPCF; // + 1e-3f * max(abs(offset.x), abs(offset.y));
        float4 depthShadow4 = shadowDepthTexture.Gather(pointClampSampler, sampleUv);
        [unroll(4)]
        for (uint i = 0; i < 4; i ++)
        {
            // 
            float dist = depthShadow4[i] - compareDepth;
            float occluder = step(0.0, dist); // reverse z.

            // Collect occluders.
            occluders += occluder;
            occluderDistSum += dist * occluder;
        }
    }

    return (pushConsts.bContactHardenPCF) ? contactHardenPCFKernal(occluders, occluderDistSum, shadowCoord.z, pixelCount) : 1.0 - occluders / float(pixelCount);
}

struct CascadeContext
{
    float NoL;
    float pcfBias;

    // Active id. 
    bool bValid;
    uint activeCascadeId;

    // Shadow coord. 
    float3 shadowCoord;

    // 
    float2 cascadeZconvertEye;

    // 
    float cascdeSplit;
    float cascadeRadiusScale;
};

void cascadeSelected(out CascadeContext ctx, const PerframeCameraView perView, const GPUBasicData scene, uint2 workPos, float2 uv, float3 positionRS)
{
    // 
    const uint cascadeCount = pushConsts.cascadeCount;
    const uint2 renderDim   = uint2(perView.renderDimension.xy);
    const float3 lightDir   = -pushConsts.lightDirection;

    // Load vertex relative world space normal. 
    Texture2D<float4> normalTexture = TBindless(Texture2D, float4, pushConsts.normalId);
    const float3 normal = normalize(normalTexture[workPos].xyz * 2.0 - 1.0);

    // 
    ctx.NoL = saturate(dot(normal, lightDir));

    // Position offset.
    const float3 positionNormalOffset = biasNormalOffset(ctx.NoL, normal);
    const float n_01 = STBN_float1(scene.blueNoiseCtx, workPos, scene.frameCounter);

    // Cascade selected.
    ctx.activeCascadeId = 0;
    for (uint cascadeId = 0; cascadeId < cascadeCount; cascadeId ++)
    {
        const InstanceCullingViewInfo shadowView = BATL(InstanceCullingViewInfo, pushConsts.shadowViewId, cascadeId);
        const float3 positionRS_cacsade = (positionRS + positionNormalOffset) + float3(perView.cameraWorldPos.getDouble3() - shadowView.cameraWorldPos.getDouble3());

        // 
        ctx.shadowCoord = mul(shadowView.translatedWorldToClip, float4(positionRS_cacsade, 1.0)).xyz;

        // Remap to [0, 1]
        ctx.shadowCoord.xy = ctx.shadowCoord.xy * float2(0.5, -0.5) + 0.5;

        // 
        ctx.cascadeZconvertEye = shadowView.orthoDepthConvertToView.xy;
        ctx.cascdeSplit        = shadowView.orthoDepthConvertToView.z;
        ctx.cascadeRadiusScale = shadowView.orthoDepthConvertToView.w;

        // Jitter cascade level between 1 - N search width area.
        float cascadeUvPadSize = pcssWidth(ctx.cascadeRadiusScale) * lerp(1.0, pushConsts.cascadeBorderJitterCount, n_01);
        if (cascadeId < pushConsts.realtimeCascadeCount)
        {
            // SDSM don't jitter.
            cascadeUvPadSize = 0.0;
        }

        if ((ctx.shadowCoord.z > 0.0) && (ctx.shadowCoord.z < 1.0))
        {
            if (all(ctx.shadowCoord.xy > cascadeUvPadSize) && all(ctx.shadowCoord.xy < 1.0 - cascadeUvPadSize))
            {
                break;
            }
        }
        ctx.activeCascadeId ++;
    }

    // Jitter shadow depth bias.
    ctx.bValid = (ctx.activeCascadeId < cascadeCount);
    if (ctx.bValid)
    {
        float f1 = saturate(1.0 - ctx.NoL); float f2  = f1 * f1; float f4  = f2 * f2; float f8  = f4 * f4;

        //
        float j_bias_const = 1e-5f * lerp(pushConsts.biasLerpMin_const, pushConsts.biasLerpMax_const, n_01);

        ctx.pcfBias = (j_bias_const + f4 * j_bias_const + (f8 * f4) * j_bias_const);

        ctx.shadowCoord.z += ctx.pcfBias / ctx.cascadeRadiusScale * ctx.cascdeSplit;
    }
}

const bool IsLitAreaInShadow(float3 litColor)
{
    return all(litColor < kFloatEpsilon);
}

const float sampleHistorySoftShadowMask(float2 uv, float3 positionRS, in const PerframeCameraView perView, float disocclusionMask)
{
    float softShadowMask = 1.0;

    if (disocclusionMask > 0.5)
    {
        return softShadowMask;
    }

    SamplerState linearClampSampler = getLinearClampEdgeSampler(perView);

    float2 historyUv = 0.0;
    bool bHistoryValid = false;
    #if 0
    {
        const float3 positionRS_prev = positionRS + float3(perView.cameraWorldPos.getDouble3() - perView.cameraWorldPosLastFrame.getDouble3());
        float3 UVz_prev = projectPosToUVz(positionRS_prev, perView.translatedWorldToClipLastFrame);

        // 
        bHistoryValid = all(UVz_prev < 1.0) && all(UVz_prev > 0.0);
        historyUv = UVz_prev.xy;
    }
    #else
    {
        Texture2D<float2> motionVectorTexture = TBindless(Texture2D, float2, pushConsts.motionVectorId);
        float2 motionVector = motionVectorTexture.SampleLevel(linearClampSampler, uv, 0);
        historyUv = uv + motionVector;

        //
        bHistoryValid = all(historyUv < 1.0) && all(historyUv > 0.0);
    }
    #endif

    if (bHistoryValid)
    {
        Texture2D<float> softShadowMaskTexture = TBindless(Texture2D, float, pushConsts.softShadowMaskTexture);

        // Linear sampler cross sample to fix edge problem.
        softShadowMask = softShadowMaskTexture.SampleLevel(linearClampSampler, historyUv, 0);
    }

    return softShadowMask;
}

groupshared uint sIsSoftShadowArea;

#define SOFT_SHADOW_EPSOLON 1e-4f

// Evaluate sun shadow projection.
[numthreads(64, 1, 1)]
void percentageCloserSoftShadowCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;
    const CascadeShadowDepthIds shadowDepthIds = BATL(CascadeShadowDepthIds, pushConsts.shadowDepthIds, 0);

    if (localThreadIndex == 0)
    {
        sIsSoftShadowArea = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    // 
    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);
    float2 uv = (workPos + 0.5) * perView.renderDimension.zw;

    RWTexture2D<float3> rwScreenColor = TBindless(RWTexture2D, float3, pushConsts.uav);
    float3 litColor = rwScreenColor[workPos];

    float shadowValue = 1.0;
    bool bNeedEvaluateShadow = true;
    if (IsLitAreaInShadow(litColor))
    {
        shadowValue = 0.0;
        bNeedEvaluateShadow = false;
    }
    
    // 
    SamplerState pointClampSampler = getPointClampEdgeSampler(perView);


    const uint2 renderDim = uint2(perView.renderDimension.xy);
    if (any(workPos >= renderDim))
    {
        bNeedEvaluateShadow = false;
    }

    float deviceZ;
    if (bNeedEvaluateShadow)
    {
        Texture2D<float> depthTexture = TBindless(Texture2D, float, pushConsts.depthId);
        deviceZ = depthTexture[workPos];

        // Skip sky. 
        if (deviceZ <= 0.0)
        {
            bNeedEvaluateShadow = false;
        }
    }

    // Get relative world position.
    const float3 positionRS = getPositionRS(uv, max(deviceZ, kFloatEpsilon), perView);

    float disocclusionMask = 1.0f;
    if (pushConsts.disocclusionMaskId != kUnvalidIdUint32)
    {
        disocclusionMask = sampleTexture2D_float1(pushConsts.disocclusionMaskId, uv, pointClampSampler);
    }

    float softShadowMask = 1.0;
    if (pushConsts.softShadowMaskTexture != kUnvalidIdUint32)
    {
        softShadowMask = sampleHistorySoftShadowMask(uv, positionRS, perView, disocclusionMask);
    }

    CascadeContext ctx;
    ctx.bValid = false;

    // 
    [branch]
    if (bNeedEvaluateShadow)
    {
        cascadeSelected(ctx, perView, scene, workPos, uv, positionRS);
    }

    float penumbraRatio = -1.0f;
    
    // 
    [branch]
    if (ctx.bValid)
    {
        Texture2D<float> shadowDepthTexture = TBindless(Texture2D, float, shadowDepthIds.shadowDepth[ctx.activeCascadeId]);
        float4 depthShadow4 = shadowDepthTexture.Gather(pointClampSampler, ctx.shadowCoord.xy);
        {
            float occluders = 0.0f;
            float occluderDistSum = 0.0f;
            [unroll(4)]
            for (uint i = 0; i < 4; i ++)
            {
                // 
                float dist = depthShadow4[i] - ctx.shadowCoord.z;
                float occluder = step(0.0, dist); // reverse z.

                // Collect occluders.
                occluders += occluder;
                occluderDistSum += dist * occluder;
            }

            shadowValue = contactHardenPCFKernal(occluders, occluderDistSum, ctx.shadowCoord.z, 4);
        }

        // Current wave inner soft shadow mask.
        {
            const bool bFullInShadow   = (shadowValue <       SOFT_SHADOW_EPSOLON);
            const bool bFullNoInShadow = (shadowValue > 1.0 - SOFT_SHADOW_EPSOLON);

            const bool bFullInShadow_Wave = WaveActiveAllTrue(bFullInShadow);
            const bool bFullNoInShadow_Wave = WaveActiveAllTrue(bFullNoInShadow);

            const bool bSoftShadowInWave = (!bFullInShadow_Wave) && (!bFullNoInShadow_Wave);
            if (bSoftShadowInWave) 
            { 
                // Always evaluate soft shadow if wave found current lane under soft shadow area.
                softShadowMask = 1.0; 
            }
        }

        [branch]
        if (softShadowMask > 0.0)
        {
            float n_01 = STBN_float1(scene.blueNoiseCtx, workPos, scene.frameCounter);
            float angle = n_01 * 2.0 * kPI;

            // 16 Tap, search don't care of bias to reduce self occlusion.
            float2 blockSearch = pcssFindBlocker(
                shadowDepthIds,
                ctx.pcfBias, 
                ctx.cascadeRadiusScale, 
                ctx.shadowCoord, 
                pointClampSampler, 
                ctx.activeCascadeId, 
                angle);

            if (blockSearch.y > kFloatEpsilon)
            {
                // To light eye space.
                float zReceiver = -(ctx.shadowCoord.z - ctx.cascadeZconvertEye.y) / ctx.cascadeZconvertEye.x;
                float blocker   = -(blockSearch.x     - ctx.cascadeZconvertEye.y) / ctx.cascadeZconvertEye.x;

                penumbraRatio = saturate((zReceiver - blocker) / blocker); // (zReceiver - blockSearch.x) / blockSearch.x;
            }

            [branch]
            if (penumbraRatio >= 0.0)
            {
                angle = n_01 * 2.0 * kPI;

                // 4 - 32 Tap. 
                shadowValue = shadowPCF(
                    shadowDepthIds,
                    penumbraRatio,
                    ctx.pcfBias,
                    ctx.cascadeRadiusScale,
                    ctx.cascadeZconvertEye,
                    ctx.shadowCoord, 
                    pointClampSampler, 
                    ctx.activeCascadeId, 
                    angle);
            }
        }
    }

#if 1
    float3 debugCacsadeColor = 0.0;
    if (ctx.bValid)
    {
        if(ctx.activeCascadeId == 0) { debugCacsadeColor = float3(0.5, 0.0, 0.0); }
        if(ctx.activeCascadeId == 1) { debugCacsadeColor = float3(0.5, 0.0, 0.5); }
        if(ctx.activeCascadeId == 2) { debugCacsadeColor = float3(0.5, 0.5, 0.0); }
        if(ctx.activeCascadeId == 3) { debugCacsadeColor = float3(0.0, 0.5, 0.0); }
    } 
#endif

    rwScreenColor[workPos] = litColor * shadowValue; 

    // Soft shadow mask.
    {
        RWTexture2D<float> rwSoftShadowMask = TBindless(RWTexture2D, float, pushConsts.uav1);

        const bool bFullInShadow   = (shadowValue <       SOFT_SHADOW_EPSOLON);
        const bool bFullNoInShadow = (shadowValue > 1.0 - SOFT_SHADOW_EPSOLON);

        const bool bFullInShadow_Wave = WaveActiveAllTrue(bFullInShadow);
        const bool bFullNoInShadow_Wave = WaveActiveAllTrue(bFullNoInShadow);

        if (WaveIsFirstLane())
        {
            const bool bSoftShadow = (!bFullInShadow_Wave) && (!bFullNoInShadow_Wave);
            uint originValue;
            InterlockedOr(sIsSoftShadowArea, uint(bSoftShadow), originValue);
        }

        GroupMemoryBarrierWithGroupSync();
        if (localThreadIndex == 0)
        {
            rwSoftShadowMask[workGroupId] = float(sIsSoftShadowArea);
        }
    }
}



#endif // __cplusplus

#endif // SHADER_PCSS_HLSL