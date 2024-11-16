#pragma once

#include "base.h"

#ifndef __cplusplus
    #include "base.hlsli"
    #include "bindless.hlsli"
    #include "sample.hlsli"
    #include "debug.hlsli"
#endif  

// https://github.com/NVIDIAGameWorks/RTXGI-DDGI
// https://media.gdcvault.com/gdc2024/Slides/GDC+slide+presentations/Kuenlib_Quentin_Raytracing_In_Snowdrop.pdf
// light probe system inspired by DDGI and snowdrop engine. 

// One probe shoot ray count. 
#define kDDGIPerProbeRayCount 32

// One wave size for performance load balance.
#define kDDGITraceThreadCount 32

// How many thread group should dispatch for one probe ray trace.
#define kDDGIProbeRayTraceThreadGroupCount ((kDDGIPerProbeRayCount + kDDGITraceThreadCount - 1) / kDDGITraceThreadCount)
#define kDDGIProbeRayTraceRoundCount (kDDGIProbeRayTraceThreadGroupCount * kDDGITraceThreadCount)

// 14x14 and 2 pixel border. 
#define kDDGIProbeDistanceTexelNum 16

// 6x6 and 2 pixel border. 
#define kDDGIProbeIrradianceTexelNum 8

// Per probe cache trace info.
struct DDGIProbeCacheTraceInfo
{
    // The world space position when do ray trace. 
    GPUStorageDouble4 worldPosition;

    // The probe ray trace direction rotation.
    float4x4 rayRotation;
};
CHORD_CHECK_SIZE_GPU_SAFE(DDGIProbeCacheTraceInfo);

// Per ray cache trace info.
struct DDGIProbeCacheMiniGbuffer
{
    float3 normalRS;
    float  distance;

    float3 baseColor;
    float   metallic;

    // Ray hit triangle back face.
    bool isRayHitBackface() { return distance < 0.0f; }
    void setRayHitDistance(float inDistance, bool bHitBackface) { distance = bHitBackface ? inDistance * -0.2 : inDistance; }
};
CHORD_CHECK_SIZE_GPU_SAFE(DDGIProbeCacheMiniGbuffer);

// Per ddgi volume config infos.
struct DDGIVoulmeConfig
{
    // Volume probe layout dimension. 
    int3 probeDim; // 
    int  rayHitSampleTextureLod; //

    int3  scrollOffset; // 
    float rayTraceStartDistance; // 

    float3 probeCenterRS; // 
    float  rayTraceMaxDistance; //

    float3 probeSpacing; //
    float  hysteresis;  // 

    float2 distanceTexelSize; //
    float2 irradianceTexelSize; //

    float probeNormalBias; // 
    float probeViewBias; // 
    uint  linearSampler; // 
    float probeDistanceExponent; // 

    int3 currentScrollOffset; //
    uint distanceSRV; //

    uint irradianceSRV; //
    bool bHistoryValid;
    uint probeHistoryValidSRV;
    float probeMinFrontfaceDistance;

    uint offsetSRV;
    uint probeCacheInfoSRV;
    uint probeFrameFill;
    uint pad2;

#ifndef __cplusplus

    int getProbeCount()
    {
        return probeDim.x * probeDim.y * probeDim.z;
    }

    // Get virtual volume id from linear probe index.
    int3 getVirtualVolumeId(int linearProbeIndex)
    {
        int dim_xy = probeDim.x * probeDim.y;
        int dim_x  = probeDim.x;

        // 
        int z = (linearProbeIndex / dim_xy);
        int y = (linearProbeIndex - z * dim_xy) / dim_x;
        int x = (linearProbeIndex - z * dim_xy - y * dim_x);

        return int3(x, y, z);
    }

    // Convert virtual volume id to physical volume id. 
    int3 getPhysicalVolumeId(int3 virtualIndex)
    {
        return (virtualIndex - scrollOffset) % probeDim;
    }

    int getLinearProbeIndex(int3 index)
    {
        int dim_xy = probeDim.x * probeDim.y;
        int dim_x  = probeDim.x;

        return index.x + index.y * dim_x + index.z * dim_xy;
    }

    int getPhysicalLinearVolumeId(int3 virtualIndex)
    {
        return getLinearProbeIndex(getPhysicalVolumeId(virtualIndex));
    }

    int2 getProbeTexelIndex(int3 physicsIndex, int probeNumTexels)
    {
        const int2 probe2dGrid = int2(physicsIndex.x, physicsIndex.z * probeDim.y + physicsIndex.y);
        return probe2dGrid * probeNumTexels;
    }

    float3 getPositionRS(int3 virtualProbeIndex)
    {
        float3 probeGridWorldPosition = virtualProbeIndex * probeSpacing;
        float3 probeGridShift = probeSpacing * (probeDim - 1) * 0.5;
        float3 probeWorldPosition = probeCenterRS + probeGridWorldPosition - probeGridShift;

        return probeWorldPosition;
    }

    int3 getVirtualVolumeIdFromPosition(float3 positionRS)
    {
        float3 probeGridShift = probeSpacing * (probeDim - 1) * 0.5;
        float3 probeGridWorldPosition = positionRS - probeCenterRS + probeGridShift;
        int3 virtualProbeIndex = int3(probeGridWorldPosition / probeSpacing);

        return clamp(virtualProbeIndex, 0, probeDim - 1);
    }

    float3 getSampleRayDir(int rayIndex)
    {
        return sphericalFibonacci(rayIndex, kDDGIPerProbeRayCount, 0.0);
    }

    float getBlendWeight(float3 positionRS)
    {
        float3 extent = probeSpacing * (probeDim - 1.0) * 0.5;
        float3 delta = abs(positionRS - probeCenterRS) - extent; // -1 for tri-linear sampler. 

        // Full in range. 
        if (all(delta < 0)) 
        { 
            return 1.0f;
        } 

        // Adjust the blend weight for each axis
        float volumeBlendWeight = 1.f;
        volumeBlendWeight *= (1.f - saturate(delta.x / probeSpacing.x));
        volumeBlendWeight *= (1.f - saturate(delta.y / probeSpacing.y));
        volumeBlendWeight *= (1.f - saturate(delta.z / probeSpacing.z));

        return volumeBlendWeight;
    }

    // RS surface bias along surface normal and view ray direction.
    float3 getSurfaceBias(float3 normalRS, float3 viewRayDirRS)
    {
        return (normalRS * probeNormalBias) - (viewRayDirRS * probeViewBias);
    }

    float2 getProbeSampleUv(float3 direction, float2 texelSize, int3 virtualIndex, int texelNum)
    {
        int3 physicsIndex = getPhysicalVolumeId(virtualIndex);
        float2 octantCoords = octahedralEncode(direction) * 0.5 + 0.5; // [0, 1]

        int2 fragCoord  = getProbeTexelIndex(physicsIndex, texelNum); // Base of the probe. 
             fragCoord += octantCoords * (texelNum - 2.0);
             fragCoord += 1; // border. 

        return texelSize * fragCoord;
    }

    float3 sampleIrradiance(in const PerframeCameraView perView, float3 positionRS, float3 surfaceBiasRS, float3 direction)
    {
        float3 irradiance = 0.0;
        float1 accumulatedWeights = 0.0;

        // Bias sample position avoid self shadow. 
        const float3 biasPositionRS = positionRS + surfaceBiasRS;

        // 
        const int3   baseProbeCoords = getVirtualVolumeIdFromPosition(biasPositionRS);
        const float3 baseProbePositionRS = getPositionRS(baseProbeCoords);
        const float3 gridSpaceDistance = biasPositionRS - baseProbePositionRS;
        const float3 alpha = saturate(gridSpaceDistance / probeSpacing);

        SamplerState linearClampSampler = Bindless(SamplerState, linearSampler);

        // Used closest 8 probe. 
        for (int i = 0; i < 8; i ++)
        {
            int3 adjacentProbeOffset = int3(i, i >> 1, i >> 2) & 1;
            int3 adjacentProbeCoords = clamp(baseProbeCoords + adjacentProbeOffset, 0, probeDim - 1);



            const int historyValidState = BATL(int, probeHistoryValidSRV, getPhysicalLinearVolumeId(adjacentProbeCoords));
            if (historyValidState == 0)
            {
                continue;
            }

            // Now get adjacent probe position.
            const int  adjacentPhysicsProbeLinearIndex = getPhysicalLinearVolumeId(adjacentProbeCoords);
            float3 adjacentProbePositionRS;
            {
                DDGIProbeCacheTraceInfo probeCacheTraceInfo = BATL(DDGIProbeCacheTraceInfo, probeCacheInfoSRV, adjacentPhysicsProbeLinearIndex);
                adjacentProbePositionRS = float3(probeCacheTraceInfo.worldPosition.getDouble3() - perView.cameraWorldPos.getDouble3());
            }


            float3 biasedPosToAdjProbe = normalize(adjacentProbePositionRS - biasPositionRS);
            float  biasedPosToAdjProbeDist = length(adjacentProbePositionRS - biasPositionRS);

            float2 octUv = getProbeSampleUv(-biasedPosToAdjProbe, distanceTexelSize, adjacentProbeCoords, kDDGIProbeDistanceTexelNum);
            check(all(octUv >= 0.0) && all(octUv <= 1.0));
            float2 filteredDistance = sampleTexture2D_float2(distanceSRV, octUv, linearClampSampler);

            // 
            float1 weight = 1.f;

            // Apply wrap shading style weight.
            {
                // 
                float3 posRSToAdjProbe = normalize(adjacentProbePositionRS - positionRS);

                // A naive soft backface weight would ignore a probe when
                // it is behind the surface. That's good for walls, but for
                // small details inside of a room, the normals on the details
                // might rule out all of the probes that have mutual visibility 
                // to the point. We instead use a "wrap shading" test. The small
                // offset at the end reduces the "going to zero" impact.
                float wrapShading = (dot(posRSToAdjProbe, direction) + 1.f) * 0.5f;
                weight *= (wrapShading * wrapShading) + 0.2f;
            }

            // Apply chebyshev occlusion test weight to avoid leaking.
            if (biasedPosToAdjProbeDist > filteredDistance.x) // occluded
            {
                float variance = abs((filteredDistance.x * filteredDistance.x) - filteredDistance.y);
                float v = biasedPosToAdjProbeDist - filteredDistance.x;

                float chebyshevWeight = variance / (variance + (v * v));

                chebyshevWeight = max((chebyshevWeight * chebyshevWeight * chebyshevWeight), 0.f);

                // Avoid visibility weights ever going all the way to zero because
                // when *no* probe has visibility we need a fallback value
                weight *= max(0.05f, chebyshevWeight);
            }

            // Avoid a weight of zero
            weight = max(0.000001f, weight);

            // A small amount of light is visible due to logarithmic perception, so
            // crush tiny weights but keep the curve continuous
            const float crushThreshold = 0.2f;
            if (weight < crushThreshold)
            {
                weight *= (weight * weight) * (1.f / (crushThreshold * crushThreshold));
            }

            // Apply the trilinear weights
            {
                //
                float3 trilinear = max(0.001f, lerp(1.f - alpha, alpha, adjacentProbeOffset));
                float1 trilinearWeight = (trilinear.x * trilinear.y * trilinear.z);

                weight *= trilinearWeight + 0.001f;
            }

            octUv = getProbeSampleUv(direction, irradianceTexelSize, adjacentProbeCoords, kDDGIProbeIrradianceTexelNum);
            check(all(octUv >= 0.0) && all(octUv <= 1.0));
            float3 probeIrradiance = sampleTexture2D_float3(irradianceSRV, octUv, linearClampSampler);

            // probeIrradiance = fastTonemapInvert(probeIrradiance);

            irradiance += (weight * probeIrradiance);
            accumulatedWeights += weight;
        }

        if (accumulatedWeights > kFloatEpsilon)
        {
            irradiance *= (1.f / accumulatedWeights);   // Normalize by the accumulated weights
            irradiance *= kPI * 2.0;                    // Multiply by the area of the integration domain (hemisphere) to complete the Monte Carlo Estimator equation
        }

        return irradiance;
    }
#endif
};
CHORD_CHECK_SIZE_GPU_SAFE(DDGIVoulmeConfig);

