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
        const uint4 packProbeSpawnInfo = BATL(uint4, pushConsts.probeSpawnInfoSRV, probeLinearIndex);
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
        const uint4 packProbeSpawnInfo = BATL(uint4, pushConsts.probeSpawnInfoSRV, probeLinearIndex);
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
            { 
                uint2 pos = uint2(probeCoordx4[i] & 0xffffu, (probeCoordx4[i] >> 16u) & 0xffffu);
                uint sampleProbeLinearId = pos.x + pos.y * pushConsts.probeDim.x;
                const uint4 packProbeSpawnInfo = BATL(uint4, pushConsts.probeSpawnInfoSRV, sampleProbeLinearId);
                sampleSpawnProbeInfo.unpack(packProbeSpawnInfo);
            }

            // 
            const float1 seed_deviceZ = sampleSpawnProbeInfo.depth;
            const float2 seed_uv = (sampleSpawnProbeInfo.pixelPosition + 0.5) / pushConsts.gbufferDim;

            // 
            float3 seed_positionRS = getPositionRS(seed_uv, seed_deviceZ, perView); 

            // 
            float viewProjectDistanceThreshold = pixelToEye * 0.25; // TODO: Try some non-linear mapping.
            if (abs(dot(seed_positionRS - pixel_positionRS, pixel_normalRS)) > viewProjectDistanceThreshold) 
            {
                probeWeightx4[i] = 0.0;
            }
            else
            {
                float distanceScale = pixelToEye * 0.25; // TODO: Try some non-linear mapping.

                // Probe dist to current pixel. 
                float dist = length(seed_positionRS - pixel_positionRS);

                float normalFactor = saturate(dot(pixel_normalRS, spawnInfo.normalRS));
                float distFactor = saturate(1.0 - dist / distanceScale);

                probeWeightx4[i] = pow(normalFactor * distFactor, 8.0);
            } 

        }
    }

    float cosineSH[9];
    sh3_coefficientsClampedCosine(pixel_normalRS, cosineSH);

    // Accumulate 2x2 screen probe irradiance.
    float3 irradiance = 0.0;
    float weightSum = 0.0;
    for (uint i = 0; i < 4; i ++)
    {
        if (probeWeightx4[i] > 0.0 && probeCoordx4[i] != kUnvalidIdUint32)
        {
            uint2 pos = uint2(probeCoordx4[i] & 0xffffu, (probeCoordx4[i] >> 16u) & 0xffffu);
            uint sampleProbeLinearId = pos.x + pos.y * pushConsts.probeDim.x;

            float3 sampleIrradiance = 0.0;
            for (uint j = 0; j < 9; j ++)
            {
                // Load SH. 
                float3 sh = BATL(float3, pushConsts.screenProbeSHSRV, sampleProbeLinearId * 9 + j);

                // Accumulate with cosine weight. 
                sampleIrradiance += cosineSH[j] * sh;
            }

            irradiance += probeWeightx4[i] * sampleIrradiance;
            weightSum  += probeWeightx4[i];
        }
    }

    // Normalize. 
    irradiance /= max(weightSum, kFloatEpsilon) * 2.0 * kPI;

    if (weightSum < 1e-3f)
    {
        // Small weight, just return zero. 
        irradiance = 0.0;
    }
    else
    {
        // Avoid negative irradiance. 
        irradiance = max(0.0, irradiance); 

        // Avoid nan produce. 
        if (any(isnan(irradiance)))
        {
            irradiance = 0.0;
        }
    }

    storeRWTexture2D_float3(pushConsts.diffuseGIUAV, tid, irradiance);
}

#endif 