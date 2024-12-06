#include "gi.h"

struct GIScreenProbeInterpolatePushConsts
{
    uint2 probeDim;
    uint2 gbufferDim;

    uint cameraViewId;
    uint probeSpawnInfoSRV;
    uint normalRSId;
    uint depthSRV;

    uint diffuseGIUAV;
    uint screenProbeSHSRV;

    uint clipmapConfigBufferId;
    uint clipmapCount; // 2 4 8
    uint screenProbeSampleSRV;

    uint reprojectSRV;
    uint motionVectorId;
    uint radiusUAV;
};
CHORD_PUSHCONST(GIScreenProbeInterpolatePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

uint findClosetProbe(int2 probeCoord, int2 offset)
{
    // Actually probe coord. 
    int2 pos = probeCoord + offset;  

    // Skip out of bound probe. 
    if (any(pos < 0) || any(pos >= pushConsts.probeDim))
    {
        return kUnvalidIdUint32;
    }

    // Load probe info. 
    const uint probeLinearIndex = pos.x + pos.y * pushConsts.probeDim.x;
    GIScreenProbeSpawnInfo spawnInfo;
    { 
        const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.probeSpawnInfoSRV, probeLinearIndex);
        spawnInfo.unpack(packProbeSpawnInfo);
    }

    // Skip invalid probe.
    if (!spawnInfo.isValid())
    {
        return kUnvalidIdUint32;
    } 
 
    // Pack coord.
    return uint(pos.x) | (uint(pos.y) << 16u); 
}   
  
[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    const uint2 gid = remap8x8(localThreadIndex); // [0x0 - 8x8)
    const uint2 tid = workGroupId * 8 + gid;
    if (any(tid >= pushConsts.gbufferDim))
    {
        // Out of bound pre-return.  
        return; 
    }

    const float pixel_deviceZ = loadTexture2D_float1(pushConsts.depthSRV, tid);

    // Skip sky pixel. 
    if (pixel_deviceZ < kFloatEpsilon)
    {
        storeRWTexture2D_float3(pushConsts.diffuseGIUAV, tid, 0.0);
        return;
    }

    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // Remap screen probe coord. 
    const uint2 probeCoord = tid / 8;

    // Load spawn info. 
    GIScreenProbeSpawnInfo spawnInfo;
    { 
        const uint probeLinearIndex = probeCoord.x + probeCoord.y * pushConsts.probeDim.x;
        const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.probeSpawnInfoSRV, probeLinearIndex);
        spawnInfo.unpack(packProbeSpawnInfo);
    }

    // Load current pixel normal. 
    float3 pixel_normalRS = loadTexture2D_float4(pushConsts.normalRSId, tid).xyz * 2.0 - 1.0;
    pixel_normalRS = normalize(pixel_normalRS);
 
    const float2 pixel_uv = (tid + 0.5) / pushConsts.gbufferDim;

    // Get current pixel world position.
    const float3 pixel_positionRS = getPositionRS(pixel_uv, max(pixel_deviceZ, kFloatEpsilon), perView); 
    const float pixelToEye = max(1e-3f, length(pixel_positionRS)); 


    // Use closet 2x2 screen probe. 
    int2 offset = select(tid < spawnInfo.pixelPosition, -1, +1);

    // Check probe is valid or not.
    uint4 probeCoordx4 = kUnvalidIdUint32;
    probeCoordx4.x = findClosetProbe(probeCoord, offset * int2(0, 0));
    probeCoordx4.y = findClosetProbe(probeCoord, offset * int2(1, 0));
    probeCoordx4.z = findClosetProbe(probeCoord, offset * int2(0, 1)); 
    probeCoordx4.w = findClosetProbe(probeCoord, offset * int2(1, 1)); 
 
    // Skip repeated probe. 
    if (probeCoordx4.y == probeCoordx4.x) 
    {
        probeCoordx4.y = kUnvalidIdUint32;
    }                                                
    if (probeCoordx4.z == probeCoordx4.y || probeCoordx4.z == probeCoordx4.x) 
    {
        probeCoordx4.z = kUnvalidIdUint32;
    }               
    if (probeCoordx4.w == probeCoordx4.z || probeCoordx4.w == probeCoordx4.y || probeCoordx4.w == probeCoordx4.x) 
    {
        probeCoordx4.w = kUnvalidIdUint32;
    }
    
    // Compute 2x2 screen probe weights. 
    float4 probeWeightx4 = 0.0;
    for (uint i = 0; i < 4; i++) 
    {
        // Only check valid probe weight.
        if (probeCoordx4[i] != kUnvalidIdUint32) 
        {
            GIScreenProbeSpawnInfo sampleSpawnProbeInfo;
            uint sampleCount;
            { 
                uint2 pos = uint2(probeCoordx4[i] & 0xffffu, (probeCoordx4[i] >> 16u) & 0xffffu);
                uint sampleProbeLinearId = pos.x + pos.y * pushConsts.probeDim.x;

                const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.probeSpawnInfoSRV, sampleProbeLinearId);
                sampleSpawnProbeInfo.unpack(packProbeSpawnInfo);


                sampleCount = loadTexture2D_uint1(pushConsts.screenProbeSampleSRV, pos);
            }

            // 
            float sampleWeight = 1.0 - 1.0 / (1.0 + sampleCount);

            // 
            float3 seed_positionRS = sampleSpawnProbeInfo.getProbePositionRS(pushConsts.gbufferDim, perView); 

            // 
            float threshold = pixelToEye * 0.25 * kMinProbeSpacing; 
            if (abs(dot(seed_positionRS - pixel_positionRS, pixel_normalRS)) > threshold) 
            {
                probeWeightx4[i] = 0.0;
            }
            else
            {
                // Probe dist to current pixel. 
                float dist = length(seed_positionRS - pixel_positionRS);

                float normalFactor = saturate(dot(pixel_normalRS, sampleSpawnProbeInfo.normalRS));
                float distFactor = saturate(1.0 - dist / threshold);

                probeWeightx4[i] = pow(normalFactor * distFactor, 8.0) * sampleWeight;
            } 
        }
    }

    // Accumulate 2x2 screen probe irradiance.
    float3 irradiance = 0.0;
    float weightSum = 0.0;

    SH3_gi composite_sh_gi;
    composite_sh_gi.init();

    for (uint i = 0; i < 4; i ++)
    {
        if (probeWeightx4[i] > 0.0 && probeCoordx4[i] != kUnvalidIdUint32)
        {
            uint2 pos = uint2(probeCoordx4[i] & 0xffffu, (probeCoordx4[i] >> 16u) & 0xffffu);
            uint sampleProbeLinearId = pos.x + pos.y * pushConsts.probeDim.x;

            SH3_gi gi_sh;
            {
                SH3_gi_pack sh_pack = BATL(SH3_gi_pack, pushConsts.screenProbeSHSRV, sampleProbeLinearId);
                gi_sh.unpack(sh_pack); 
            }

            irradiance += probeWeightx4[i] * SH_Evalulate(pixel_normalRS, gi_sh.c);
            weightSum  += probeWeightx4[i];
        }
    }

    float  world_weight = 0.0;
    float3 world_irradiance = 0.0;
    for (uint cascadeId = 0; cascadeId < pushConsts.clipmapCount; cascadeId ++)  
    {
        if (world_weight >= 1.0)
        {
            break;
        }

        const GIWorldProbeVolumeConfig config = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, cascadeId);
        float weight = config.getBlendWeight(pixel_positionRS);

        [branch]   
        if (weight <= 0.0)
        { 
            continue;
        }
        else 
        {
            float3 radiance = 0.0;
            bool bSampleValid = config.evaluateSH(perView, pixel_positionRS, pixel_normalRS, radiance, true);

            weight = bSampleValid ? weight : 0.0;
            float linearWeight = (world_weight > 0.0) ? (1.0 - world_weight) : weight;
    
            world_weight += linearWeight;
            world_irradiance += linearWeight * radiance;
        }  
    }   
    world_irradiance = (world_weight > 0.0) ? (world_irradiance / world_weight) : 0.0;

    if (any(isnan(world_irradiance)))
    {
        world_irradiance = 0.0;
    }

    // Sample world probe as fallback. 
    if (weightSum < 1e-3f)
    {
        irradiance = world_irradiance;
    }
    else
    {
        irradiance /= max(weightSum, kFloatEpsilon) * 2.0 * kPI;
    }

    // Avoid negative irradiance. 
    irradiance = max(0.0, irradiance); 
    float resultSample = 0.0;

    const bool bHistoryValid = pushConsts.reprojectSRV != kUnvalidIdUint32;
    if (bHistoryValid)
    {
        float4 reprojected = loadTexture2D_float4(pushConsts.reprojectSRV, tid);
        
        // Stable screen probe.  
        irradiance = lerp(irradiance, world_irradiance, 1.0 - saturate(weightSum));
        irradiance = lerp(irradiance, world_irradiance, smoothstep(0.0, 1.0, 1.0 / (1.0 + reprojected.w)));

        // 
        reprojected.xyz = giClipAABB(reprojected.xyz, irradiance, 0.2);

        irradiance = lerp(reprojected.xyz,  irradiance, 1.0 / (1.0 + reprojected.w));



//      irradiance = lerp(irradiance, world_irradiance, 1.0 - pow(min(reprojected.w + 1.0, kGIMaxSampleCount) / kGIMaxSampleCount, 1.25));
        // irradiance = world_irradiance;
        resultSample = reprojected.w;
    }

    float4 result = float4(irradiance, min(kGIMaxSampleCount, resultSample + 1.0));
    if (any(isnan(result)))
    {
        result = 0.0;
    }

    storeRWTexture2D_float4(pushConsts.diffuseGIUAV, tid, result);

 
    float sampleRadiusWeight = saturate(1.0 - pow(result.w / kGIMaxSampleCount, 4.0));
    float screenProbeInterpolateWeight = 0.0;
#if 1
    screenProbeInterpolateWeight = 1.0 - saturate(weightSum * 0.5);
#endif

    // 
    float filterRadius = max(sampleRadiusWeight, screenProbeInterpolateWeight);
    storeRWTexture2D_float1(pushConsts.radiusUAV, tid, filterRadius);
}

#endif 