#ifndef SHADER_GI_SCREEN_PROBE_SH_REPROJECTION_HLSL
#define SHADER_GI_SCREEN_PROBE_SH_REPROJECTION_HLSL

#include "gi.h"

// History screen probe SH reprojection. 
struct GIScreenProbeSHReprojectPushConst
{
    uint2 probeDim;
    uint2 gbufferDim;

    uint cameraViewId;
    uint motionVectorId;

    uint probeSpawnInfoSRV;
    uint historyProbeSpawnInfoSRV;

    uint reprojectionStatUAV;

    uint historyProbeTraceRadianceSRV;
    uint reprojectionRadianceUAV;
    uint screenProbeSampleUAV;
    uint bResetAll;
};
CHORD_PUSHCONST(GIScreenProbeSHReprojectPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#define DEBUG_REPROJECT_SAMPLE 0

groupshared uint sUint_0[64];
groupshared uint sUint_1[64];
groupshared uint sUint_2[64];
groupshared uint sUint_3[64];
groupshared uint sUint_4[64];

groupshared uint sReprojectCount;
groupshared float sTileWeight[9];

void ldsStoreRadiance(int idx, float4 r)
{
    uint2 packR = pack_float4_t_uint2(r);
    sUint_0[idx] = packR.x;
    sUint_1[idx] = packR.y;
}

float4 ldsLoadRadiance(int idx)
{
    uint2 packR;
    packR.x = sUint_0[idx];
    packR.y = sUint_1[idx];
    return unpack_float4_f_uint2(packR);
}

float weightQuantize(float w) 
{ 
    return float(uint(w * 1.0e6f)) * 1.0e-6f; 
}

uint4 radianceQuantize(float4 radiance) 
{ 
    return uint4(floor(1.0e4f * radiance)); 
}

float4 radianceDequantize(uint4 qRadiance) 
{ 
    return float4(qRadiance) / 1.0e4f; 
}

float giReprojectWeight(float3 p0, float3 p1, float e, float power)
{
    float distanceFactor = saturate(1.0 - length(p0 - p1) / e);
    return pow(distanceFactor, power);
}

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    if (localThreadIndex == 0)
    {
        sUint_1[0] = 0;
        sReprojectCount = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint2 gid = remap8x8(localThreadIndex); 
    const uint2 tid = workGroupId * 8 + gid;

    const uint2 probeCoord = workGroupId;
    const uint probeLinearIndex = probeCoord.x + probeCoord.y * pushConsts.probeDim.x;

    GIScreenProbeSpawnInfo cur_spawnInfo;
    { 
        const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.probeSpawnInfoSRV, probeLinearIndex);
        cur_spawnInfo.unpack(packProbeSpawnInfo);
    }

    // Non valid spawn probe in current frame, reset history. 
    if (!cur_spawnInfo.isValid() || perView.bCameraCut || pushConsts.bResetAll)
    {
        if (localThreadIndex == 0)
        {
            storeRWTexture2D_float3(pushConsts.reprojectionStatUAV, probeCoord, 0.0);
            storeRWTexture2D_uint1(pushConsts.screenProbeSampleUAV, probeCoord, 0);
        }
        storeRWTexture2D_float4(pushConsts.reprojectionRadianceUAV, tid, 0.0);
        return;
    }

    // 
    float2 pixel_uv = (tid + 0.5) / pushConsts.gbufferDim;
    float2 pixel_history_uv = pixel_uv + loadTexture2D_float2(pushConsts.motionVectorId, tid);

    // 
    float3 probe_positionRS = cur_spawnInfo.getProbePositionRS(pushConsts.gbufferDim, perView);

    float probe_eps_size = length(probe_positionRS) * kMinProbeSpacing;
    if (all(pixel_history_uv >= 0.0) && all(pixel_history_uv <= 1.0))
    {
        uint2 sample_history_coord = pixel_history_uv * pushConsts.gbufferDim;
        uint2 sample_history_probe_coord = sample_history_coord / 8;
        uint sample_history_probe_idx = sample_history_probe_coord.x + sample_history_probe_coord.y * pushConsts.probeDim.x;

        GIScreenProbeSpawnInfo sample_history_spawnInfo;
        { 
            const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.historyProbeSpawnInfoSRV, sample_history_probe_idx);
            sample_history_spawnInfo.unpack(packProbeSpawnInfo);
        }

        if (sample_history_spawnInfo.isValid())
        {
            float3 sample_probe_history_position = sample_history_spawnInfo.getProbePositionRS_LastFrame(pushConsts.gbufferDim, perView);

            // Distance diff weight. 
            float w = giReprojectWeight(probe_positionRS, sample_probe_history_position, probe_eps_size, 4.0);
            if (w > 0.1)
            {
                sUint_0[localThreadIndex] = sample_history_probe_coord.x | (sample_history_probe_coord.y << 16u);

                // Weight ranking. 
                uint pack = (f32tof16(w) << 16u) | localThreadIndex;
                InterlockedMax(sUint_1[0], pack);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // All thread return if no
    if (sUint_1[0] == 0)
    {
        if (localThreadIndex == 0)
        {
            storeRWTexture2D_float3(pushConsts.reprojectionStatUAV, probeCoord, 0.0);
            storeRWTexture2D_uint1(pushConsts.screenProbeSampleUAV, probeCoord, 0);
        }
        storeRWTexture2D_float4(pushConsts.reprojectionRadianceUAV, tid, 0.0);
        return;
    }

    // Get most fit probe for all thread. 
    uint packProbeCoord = sUint_0[sUint_1[0] & 0xFFFFu];
    int2 baseSampleProbeCoord = uint2(packProbeCoord & 0xFFFFu, (packProbeCoord >> 16u) & 0xFFFFu);

#if DEBUG_REPROJECT_SAMPLE
    storeRWTexture2D_float4(pushConsts.reprojectionRadianceUAV, tid, loadTexture2D_float4(pushConsts.historyProbeTraceRadianceSRV, baseSampleProbeCoord * 8 + gid));
    return;
#endif

    // 3x3 neighbor probe weight. 
    if (all(gid < 3))
    {
        int2 sampleProbeCoord = (int2(gid) - 1) + baseSampleProbeCoord;
        int localTileIdx = gid.x + gid.y * 3;

        float w = 0.0;
        if (all(sampleProbeCoord >= 0) && all(sampleProbeCoord < pushConsts.probeDim))
        {
            uint sampleProbeIdx = sampleProbeCoord.x + sampleProbeCoord.y * pushConsts.probeDim.x;

            GIScreenProbeSpawnInfo sample_history_spawnInfo;
            { 
                const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.historyProbeSpawnInfoSRV, sampleProbeIdx);
                sample_history_spawnInfo.unpack(packProbeSpawnInfo);
            }

            if (sample_history_spawnInfo.isValid())
            {
                float3 sample_probe_history_position = sample_history_spawnInfo.getProbePositionRS_LastFrame(pushConsts.gbufferDim, perView);
                w = giReprojectWeight(probe_positionRS, sample_probe_history_position, probe_eps_size * 2.0, 16.0);
            }
        }
        sTileWeight[localTileIdx] = w;
    }
    GroupMemoryBarrierWithGroupSync();

    {
        sUint_0[localThreadIndex] = 0;
        sUint_1[localThreadIndex] = 0;
        sUint_2[localThreadIndex] = 0;
        sUint_3[localThreadIndex] = 0;
        sUint_4[localThreadIndex] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (int y = -1; y <= +1; y ++)
    {
        for (int x = -1; x <= +1; x ++)
        {
            int2 sampleProbeCoord = baseSampleProbeCoord + int2(x, y);
            float weight = sTileWeight[(x + 1) + (y + 1) * 3];
            if (weight < 0.1 || any(sampleProbeCoord < 0) || any(sampleProbeCoord >= pushConsts.probeDim))
            {
                continue;
            }

            #if DEBUG_ONLY_REPROJECT_CENTER
                if (x != 0 && y != 0) { continue; }
            #endif

            // One probe reproject success.
            InterlockedAdd(sReprojectCount, 1);

            uint sampleProbeIdx = sampleProbeCoord.x + sampleProbeCoord.y * pushConsts.probeDim.x;
            GIScreenProbeSpawnInfo sample_history_spawnInfo;
            { 
                const uint3 packProbeSpawnInfo = BATL(uint3, pushConsts.historyProbeSpawnInfoSRV, sampleProbeIdx);
                sample_history_spawnInfo.unpack(packProbeSpawnInfo);
            }

            float3 sample_probe_history_position = sample_history_spawnInfo.getProbePositionRS_LastFrame(pushConsts.gbufferDim, perView);
            float3 sample_ray_dir = getScreenProbeCellRayDirection(scene, sampleProbeCoord, gid, sample_history_spawnInfo.normalRS, false);
            float4 sample_radiance = loadTexture2D_float4(pushConsts.historyProbeTraceRadianceSRV, sampleProbeCoord * 8 + gid);

            // Parallax corrected reprojection
            float3 hitPoint = sample_probe_history_position + sample_ray_dir * sample_radiance.w;
            float3 reprojectDir = hitPoint - probe_positionRS;

            // Transform to hemi sphere direction. 
            float3x3 tbn = createTBN(cur_spawnInfo.normalRS); 
            float3 hemiSphereDir = normalize(tbnInverseTransform(tbn, normalize(reprojectDir)));
            if (hemiSphereDir.z > 0.0)
            {
                float2 uv = clamp(hemiOctahedralEncode(hemiSphereDir) * 0.5 + 0.5, 0.0, 0.99);
                uint2 sampleCellCoord = uv * 8;
                uint localCellIdx = sampleCellCoord.x + sampleCellCoord.y * 8;

                // New texel radiance and hit t. 
                float4 texelRadiance = float4(sample_radiance.xyz, length(reprojectDir));

                // Use atomic add and only support uint, so need to quantize. 
                float quantized_weight = weightQuantize(weight);
                uint4 quantized_radiance = radianceQuantize(quantized_weight * texelRadiance);

                InterlockedAdd(sUint_0[localCellIdx], quantized_radiance.x);
                InterlockedAdd(sUint_1[localCellIdx], quantized_radiance.y);
                InterlockedAdd(sUint_2[localCellIdx], quantized_radiance.z);
                InterlockedAdd(sUint_3[localCellIdx], quantized_radiance.w);

                //
                InterlockedAdd(sUint_4[localCellIdx], uint(quantized_weight * 1.0e6f));
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // No valid reproject count, so return. 
    if (sReprojectCount == 0)
    {
        if (localThreadIndex == 0)
        {
            storeRWTexture2D_float3(pushConsts.reprojectionStatUAV, probeCoord, 0.0);
            storeRWTexture2D_uint1(pushConsts.screenProbeSampleUAV, probeCoord, 0);
        }
        storeRWTexture2D_float4(pushConsts.reprojectionRadianceUAV, tid, 0.0);
        return;
    }

    float4 historyRadiance = radianceDequantize(uint4(
        sUint_0[localThreadIndex], 
        sUint_1[localThreadIndex], 
        sUint_2[localThreadIndex], 
        sUint_3[localThreadIndex]));

    float quantized_weight = max(1e-6f, sUint_4[localThreadIndex] * 1e-6f);
    historyRadiance /= quantized_weight; // normalize. 

    GroupMemoryBarrierWithGroupSync();
    {
        // .w now store history radiance valid state when self intersection, can used to normalize. 
        ldsStoreRadiance(localThreadIndex, historyRadiance.w < 1e-3f ? 0.0 : float4(historyRadiance.xyz, 1.0)); 
    }
    GroupMemoryBarrierWithGroupSync();

    #define REDUCE_OP(OFFSET_X) if(localThreadIndex < OFFSET_X) { ldsStoreRadiance(localThreadIndex, ldsLoadRadiance(localThreadIndex) + ldsLoadRadiance(localThreadIndex + OFFSET_X)); }
    {
        REDUCE_OP(32);
        GroupMemoryBarrierWithGroupSync();

        REDUCE_OP(16);
        REDUCE_OP(8);
        REDUCE_OP(4);
        REDUCE_OP(2);
        REDUCE_OP(1);

        GroupMemoryBarrierWithGroupSync();
    }
    #undef REDUCE_OP

    float4 stat = ldsLoadRadiance(0); // Get sum radiance value. 
    stat /= max(1e-3f, stat.w); // normalize. 
 
    // 
    if (historyRadiance.w < 0.1)
    {
        historyRadiance = stat;
    } 

    storeRWTexture2D_float4(pushConsts.reprojectionRadianceUAV, tid, historyRadiance);
    if (localThreadIndex == 0)
    {
        storeRWTexture2D_float3(pushConsts.reprojectionStatUAV, probeCoord, stat.xyz);

        uint srcSampleCount = loadRWTexture2D_uint1(pushConsts.screenProbeSampleUAV, probeCoord);
        storeRWTexture2D_uint1(pushConsts.screenProbeSampleUAV, probeCoord, min(8, srcSampleCount + 1)); // Max 8 sample.
    }
}
#endif   

#endif 