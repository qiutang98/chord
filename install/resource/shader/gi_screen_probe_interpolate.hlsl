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

    uint reprojectSpecularSRV;
    uint specularUAV;
    uint rouhnessSRV;
    uint specularTraceSRV;

    uint specularStatUAV;
    uint bJustUseWorldCache;
    uint bDisableWorldCache;
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

groupshared float4 sRadiance[64];
  
[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    const uint2 gid = remap8x8(localThreadIndex); // [0x0 - 8x8)
    const uint2 tid = workGroupId * 8 + gid;

    sRadiance[localThreadIndex] = 0.0;
    GroupMemoryBarrierWithGroupSync();

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

            // 
            if (abs(dot(seed_positionRS - pixel_positionRS, pixel_normalRS)) > threshold)
            {
                // If no in same palne, skip. 
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

    // Accumulate 2x2 screen probe diffuse_screenProbeIrradiance.
    float  screenProbeWeightSum  = 0.0;

    float3 diffuse_screenProbeIrradiance = 0.0;
    float3 specular_screenProbeIrradiance = 0.0;

    float3 rayReflectionDir = reflect(normalize(pixel_positionRS), pixel_normalRS);

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

            diffuse_screenProbeIrradiance += probeWeightx4[i] * SH_Evalulate(pixel_normalRS, gi_sh.c);
            specular_screenProbeIrradiance += probeWeightx4[i] * SH_Evalulate(rayReflectionDir, gi_sh.c);

            screenProbeWeightSum  += probeWeightx4[i];
        }
    }

    float  world_weight = 0.0;
    float3 diffuse_world_irradiance = 0.0;
    float3 specular_world_irradiance = 0.0;

    if (!pushConsts.bDisableWorldCache)
    {
        for (uint cascadeId = 0; cascadeId < pushConsts.clipmapCount; cascadeId ++)  
        {
            if (world_weight >= 1.0)
            {
                break;
            }

            const GIWorldProbeVolumeConfig config = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, cascadeId);
            float weight = config.getBlendWeight(pixel_positionRS);

            [branch]   
            if (weight <= 0.0 || config.bResetAll)
            { 
                continue;
            }
            else 
            {
                float3 diffuse_radiance = 0.0;
                float3 specular_radiane = 0.0;

                float minSampleCount;
                bool bSampleValid = config.evaluateSH(perView, pixel_positionRS, pixel_normalRS, diffuse_radiance, minSampleCount, 1);
                config.evaluateSH(perView, pixel_positionRS, rayReflectionDir, specular_radiane, minSampleCount, 1);
                
                weight = bSampleValid ? weight : 0.0;
                float linearWeight = (world_weight > 0.0) ? (1.0 - world_weight) : weight;
        
                world_weight += linearWeight;


                diffuse_world_irradiance += linearWeight * diffuse_radiance;
                specular_world_irradiance += linearWeight * specular_radiane;
            }  
        }   
    }

    diffuse_world_irradiance = (world_weight > 0.0) ? (diffuse_world_irradiance / world_weight) : 0.0;
    specular_world_irradiance = (world_weight > 0.0) ? (specular_world_irradiance / world_weight) : 0.0;

    if (any(isnan(diffuse_world_irradiance)))
    {
        diffuse_world_irradiance = 0.0;
    }
    if (any(isnan(specular_world_irradiance)))
    {
        specular_world_irradiance = 0.0;
    }

    // Sample world probe as fallback. 
    if (screenProbeWeightSum < 1e-3f)
    {
        specular_screenProbeIrradiance = specular_world_irradiance;
        diffuse_screenProbeIrradiance = diffuse_world_irradiance;
    }
    else
    {
        diffuse_screenProbeIrradiance /= max(screenProbeWeightSum, kFloatEpsilon) * 2.0 * kPI;
        specular_screenProbeIrradiance /= max(screenProbeWeightSum, kFloatEpsilon) * 2.0 * kPI;
    }



    // Avoid negative diffuse_screenProbeIrradiance. 
    diffuse_screenProbeIrradiance = max(0.0, diffuse_screenProbeIrradiance); 
    specular_screenProbeIrradiance = max(0.0, specular_screenProbeIrradiance);
    float resultSample = 0.0;

#if 0
    // Screen edge use world fall back.
    if (world_weight > 0.0)
    {
        float2 fov = 0.05 * float2(perView.renderDimension.y / perView.renderDimension.x, 1);

        float2 border = smoothstep(0, fov, pixel_uv) * (1 - smoothstep(1 - fov, 1.0, pixel_uv));
        float vignette = border.x * border.y;

        diffuse_screenProbeIrradiance = lerp(diffuse_world_irradiance, diffuse_screenProbeIrradiance, vignette);
    }
#endif

    if (pushConsts.bJustUseWorldCache && world_weight > 0.0)
    {
        diffuse_screenProbeIrradiance = diffuse_world_irradiance;
    }

    if (pushConsts.reprojectSRV != kUnvalidIdUint32)
    {
        float4 reprojected = loadTexture2D_float4(pushConsts.reprojectSRV, tid);
//      float4 reprojected = sampleTexture2D_float4(pushConsts.reprojectSRV, pixel_uv, getLinearClampEdgeSampler(perView));
        
        if (any(reprojected > 0.0))
        {
            // Stable screen probe.  
            diffuse_screenProbeIrradiance = lerp(diffuse_screenProbeIrradiance, diffuse_world_irradiance, 1.0 - saturate(screenProbeWeightSum));
            diffuse_screenProbeIrradiance = lerp(diffuse_screenProbeIrradiance, diffuse_world_irradiance, smoothstep(0.0, 1.0, 1.0 / (1.0 + reprojected.w)));

            float3 maxIrradiance_wave = WaveActiveMax(diffuse_screenProbeIrradiance);
            float3 minIrradiance_wave = WaveActiveMin(diffuse_screenProbeIrradiance);
            

            float3 boundIrradiance_wave = maxIrradiance_wave - minIrradiance_wave;


            // TODO: Split interpolate and clip rectify, which can add some spatial filter before clip AABB, help reduce flicker.
            float3 avgColor_wave = WaveActiveSum(diffuse_screenProbeIrradiance) / WaveActiveSum(true);
            float clipHistoryAABBSize_GI = lerp(0.6, 0.2, saturate(screenProbeWeightSum));
            reprojected.xyz = giClipAABB(reprojected.xyz, avgColor_wave, clipHistoryAABBSize_GI);

            // reprojected.xyz = giClipAABB(reprojected.xyz, diffuse_screenProbeIrradiance, clipHistoryAABBSize_GI + boundIrradiance_wave);
            // reprojected.xyz = clipAABB_compute(minIrradiance_wave, maxIrradiance_wave, reprojected.xyz, 0.001);

            diffuse_screenProbeIrradiance = lerp(reprojected.xyz,  diffuse_screenProbeIrradiance, 1.0 / (1.0 + reprojected.w));

    //      diffuse_screenProbeIrradiance = lerp(diffuse_screenProbeIrradiance, diffuse_world_irradiance, 1.0 - pow(min(reprojected.w + 1.0, kGIMaxSampleCount) / kGIMaxSampleCount, 1.25));
    //      diffuse_screenProbeIrradiance = diffuse_world_irradiance;
            resultSample = reprojected.w;
        }

    }

    float3 specularRadiance = 0.0;
    float specularSample  = 0.0;

    float4 specularTraceRadiance = loadTexture2D_float4(pushConsts.specularTraceSRV, tid);
    if(any(isnan(specularTraceRadiance)))
    {
        specularTraceRadiance = 0.0;
    }
    
    float roughness = loadTexture2D_float1(pushConsts.rouhnessSRV, tid);
    if (roughness == 0.0) 
    {
        specularRadiance = specularTraceRadiance.xyz; // Mirror surface. 
        specularSample = 1.0;

        check(!any(isnan(specularTraceRadiance.xyz)));
    }
    else
    {
    #if 0
        float lerpFactor = roughness; // saturate(roughness * (1.0 / kGIMaxGlossyRoughness));
        // specularTraceRadiance.xyz = lerp(specularTraceRadiance.xyz, specular_screenProbeIrradiance, pow(lerpFactor, 16.0));
    #else 
        float lerpFactor =  saturate(roughness * (1.0 / kGIMaxGlossyRoughness));
        specularTraceRadiance.xyz = lerp(specularTraceRadiance.xyz, specular_screenProbeIrradiance, pow(lerpFactor, 16.0));
    #endif


        specularSample = 0.0;
        specularRadiance = specularTraceRadiance.xyz; 
 
        if (pushConsts.reprojectSpecularSRV != kUnvalidIdUint32)
        {
            float4 reprojected = loadTexture2D_float4(pushConsts.reprojectSpecularSRV, tid);
            if (any(reprojected > 0.0))
            {
                specularSample = reprojected.w;
                
                const float maxSample = lerp(8.0, kGIMaxSampleCount, sqrt(lerpFactor));
                float weight = 1.0 / (1.0 + specularSample);

                // TODO: Split interpolate and clip rectify, which can add some spatial filter before clip AABB, help reduce flicker.
                float3 avgColor_wave = WaveActiveSum(specularTraceRadiance.xyz) / WaveActiveSum(true);
                float clipHistoryAABBSize_Specular = lerp(0.6, 0.2, saturate(screenProbeWeightSum));
                reprojected.xyz = giClipAABB(reprojected.xyz, avgColor_wave, clipHistoryAABBSize_Specular);

                specularRadiance = lerp(reprojected.xyz, specularTraceRadiance.xyz, weight);
                specularSample  = min(maxSample, specularSample + 1.0); 

                // check(!any(isnan(specularRadiance)));
            }
        }

        specularSample ++;

        // check(!any(isnan(specularRadiance)));
    }

    storeRWTexture2D_float4(pushConsts.specularUAV, tid, float4(specularRadiance, specularSample));
                

    float4 result = float4(diffuse_screenProbeIrradiance, min(kGIMaxSampleCount, resultSample + 1.0));
    if (any(isnan(result)))
    {
        result = 0.0;
    }
    storeRWTexture2D_float4(pushConsts.diffuseGIUAV, tid, result);

 
    float sampleRadiusWeight = saturate(1.0 - pow(result.w / kGIMaxSampleCount, 4.0));
    float screenProbeInterpolateWeight = 0.0;
#if 1
    screenProbeInterpolateWeight = 1.0 - saturate(screenProbeWeightSum * 0.5);
#endif

    // 
    float filterRadius = max(sampleRadiusWeight, screenProbeInterpolateWeight);
    float specularFilterRadius = 1.0 / (1.0 + specularSample);
    storeRWTexture2D_float2(pushConsts.radiusUAV, tid, float2(filterRadius, specularFilterRadius));

    // 
    // check(!any(isnan(specularRadiance)));

    //
    if (any(workGroupId >= pushConsts.gbufferDim / 8))
    {
        return;
    }

    sRadiance[localThreadIndex] = float4(specularRadiance.xyz, any(specularRadiance > 0.0) ? 1.0 : 0.0);
    GroupMemoryBarrierWithGroupSync();

    if (localThreadIndex < 32)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 32];
    }
    GroupMemoryBarrierWithGroupSync();
    if (localThreadIndex < 16)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 16];
    }
    if (localThreadIndex < 8)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 8];
    }
    if (localThreadIndex < 4)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 4];
    }
    if (localThreadIndex < 2)
    {
        sRadiance[localThreadIndex] += sRadiance[localThreadIndex + 2];
    }
    if (localThreadIndex < 1)
    {
        float4 radianceSum = sRadiance[0] + sRadiance[1];
        float3 avgRadiance = radianceSum.w > 0.0 ? radianceSum.xyz / radianceSum.w : 0.0;

        if (pushConsts.specularStatUAV != kUnvalidIdUint32)
        {
            storeRWTexture2D_float3(pushConsts.specularStatUAV, workGroupId, avgRadiance);
        }
    }
}

#endif 