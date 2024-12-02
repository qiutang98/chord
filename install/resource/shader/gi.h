#pragma once

#include "base.h"

#ifndef __cplusplus
    #include "base.hlsli"
    #include "bindless.hlsli"
    #include "sample.hlsli"
    #include "debug.hlsli"
    #include "blue_noise.hlsli"
    #include "sh.hlsli"
#endif  

// Max cascade count for world probe clipmap. 
#define kGIWorldProbeMaxCascadeCount 8

struct SH3_gi_pack
{
    uint v[14];
};

struct SH3_gi
{
    float3 c[9];
    float numSample;

#ifndef __cplusplus
    void init()
    {
        numSample = 0.0;
        for (uint i = 0; i < 9; i ++)
        {
            c[i] = 0.0;
        }
    }

    void add(in SH3_gi o, float w = 1.0)
    {
        numSample += o.numSample * w;
        for (uint i = 0; i < 9; i ++)
        {
            c[i] += o.c[i] * w;
        }
    }

    void mul(float f)
    {
        numSample *= f;
        for (uint i = 0; i < 9; i ++)
        {
            c[i] *= f;
        }
    }

    void div(float w)
    {
        float f = 1 / w;
        mul(f);
    }



    SH3_gi_pack pack()
    {
        SH3_gi_pack r;

        // 
        uint v_i = 0;
        [unroll(4)]
        for(uint c_i = 0; c_i < 8; c_i += 2)
        {
            uint3 c0 = f32tof16(c[c_i + 0]);
            uint3 c1 = f32tof16(c[c_i + 1]);

            r.v[v_i + 0] = c0.x | (c0.y << 16u);
            r.v[v_i + 1] = c0.z | (c1.x << 16u);
            r.v[v_i + 2] = c1.y | (c1.z << 16u);

            v_i += 3;
        }

        uint sampleNum_h = f32tof16(numSample);
        uint3 c8 = f32tof16(c[8]);
        r.v[12] = c8.x | (c8.y << 16u);
        r.v[13] = c8.z | (sampleNum_h << 16u);

        return r;
    }

    void unpack(SH3_gi_pack r)
    {
        uint v_i = 0;
        [unroll(4)]
        for (uint c_i = 0; c_i < 8; c_i += 2)
        {
            uint3 c0;
            uint3 c1;

            c0.x = (r.v[v_i + 0] >>  0u) & 0xffffu;
            c0.y = (r.v[v_i + 0] >> 16u) & 0xffffu;
            c0.z = (r.v[v_i + 1] >>  0u) & 0xffffu;

            c1.x = (r.v[v_i + 1] >> 16u) & 0xffffu;
            c1.y = (r.v[v_i + 2] >>  0u) & 0xffffu;
            c1.z = (r.v[v_i + 2] >> 16u) & 0xffffu;

            c[c_i + 0] = f16tof32(c0);
            c[c_i + 1] = f16tof32(c1);

            v_i += 3; 
        }

        uint3 c8;
        c8.x = (r.v[12] >>  0u) & 0xffffu;
        c8.y = (r.v[12] >> 16u) & 0xffffu;
        c8.z = (r.v[13] >>  0u) & 0xffffu;

        uint sampleNum_h = (r.v[13] >> 16u) & 0xffffu;

        c[8] = f16tof32(c8);
        numSample = f16tof32(sampleNum_h);
    }
#endif
};

// World probe clipmap need to propagate, so don't need virtual and physics page convert here, we update when propagate.
struct GIWorldProbeVolumeConfig
{
    int3  probeDim; // 
    uint sh_UAV; // 

    float3 probeSpacing; //
    uint sh_SRV; //

    float3 probeCenterRS;
    uint pad0; //

    int3 scrollOffset; 
    int pad1; //

    int3 currentScrollOffset; 
    bool bRestAll; //

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

    bool isHistoryValid(int3 virtualId)
    {
        if (bRestAll)
        {
            return false;
        }

        int3 virtualOffset = virtualId - currentScrollOffset;
        if (any(virtualOffset < 0) || any(virtualOffset >= probeDim))
        {
            return false; // History invalid so skip. 
        } 
        return true;
    }

    int getPhysicalLinearVolumeId(int3 virtualIndex)
    {
        return getLinearProbeIndex(getPhysicalVolumeId(virtualIndex));
    }

    float3 getPositionRS(int3 virtualProbeIndex)
    {
        float3 probeGridWorldPosition = virtualProbeIndex * probeSpacing;
        float3 probeGridShift = probeSpacing * (probeDim - 1) * 0.5;
        float3 probeWorldPosition = probeCenterRS + probeGridWorldPosition - probeGridShift;

        return probeWorldPosition;
    }

    int3 getVirtualVolumeIdFromPosition(float3 positionRS, bool bClamp = true)
    {
        float3 probeGridShift = probeSpacing * (probeDim - 1) * 0.5;
        float3 probeGridWorldPosition = positionRS - probeCenterRS + probeGridShift;
        int3 virtualProbeIndex = int3(probeGridWorldPosition / probeSpacing);

        return bClamp ? clamp(virtualProbeIndex, 0, probeDim - 1) : virtualProbeIndex;
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

    bool evaluateSH(in const PerframeCameraView perView, float3 positionRS, float sampleClamp, float3 normalRS, out float3 irradiance)
    {
        irradiance = 0.0;

        // 
        float weightSum = 0.0;

        const int3 baseProbeCoords = getVirtualVolumeIdFromPosition(positionRS);
        const float3 baseProbePositionRS = getPositionRS(baseProbeCoords);
        const float3 gridSpaceDistance = positionRS - baseProbePositionRS;
        const float3 alpha = saturate(gridSpaceDistance / probeSpacing);

        float weightAccumulation = 0.0;
        for (int i = 0; i < 8; i ++)
        {
            int3 adjacentProbeOffset = int3(i, i >> 1, i >> 2) & 1;
            int3 adjacentProbeCoords = clamp(baseProbeCoords + adjacentProbeOffset, 0, probeDim - 1);

            float3 trilinear = max(0.001f, lerp(1.f - alpha, alpha, adjacentProbeOffset));
            float1 trilinearWeight = 0.001f + (trilinear.x * trilinear.y * trilinear.z);

            const int adjacentPhysicsProbeLinearIndex = getPhysicalLinearVolumeId(adjacentProbeCoords);

            SH3_gi sample_world_gi_sh;
            {
                SH3_gi_pack sh_pack = BATL(SH3_gi_pack, sh_SRV, adjacentPhysicsProbeLinearIndex);
                sample_world_gi_sh.unpack(sh_pack);
            }

            if (sample_world_gi_sh.numSample < sampleClamp)
            {
                continue;
            }

            irradiance += trilinearWeight * SH_Evalulate(normalRS, sample_world_gi_sh.c);
            weightSum  += trilinearWeight;
        }

        //
        irradiance /= max(1e-6f, weightSum) * 2.0 * kPI;

        // 
        return weightSum > 0.0;
    }

    bool sampleSH(in const PerframeCameraView perView, float3 positionRS, float sampleClamp, inout SH3_gi world_gi_sh)
    {
        world_gi_sh.init();

        const int3 baseProbeCoords = getVirtualVolumeIdFromPosition(positionRS);
        const float3 baseProbePositionRS = getPositionRS(baseProbeCoords);
        const float3 gridSpaceDistance = positionRS - baseProbePositionRS;
        const float3 alpha = saturate(gridSpaceDistance / probeSpacing);

        float weightAccumulation = 0.0;
        for (int i = 0; i < 8; i ++)
        {
            int3 adjacentProbeOffset = int3(i, i >> 1, i >> 2) & 1;
            int3 adjacentProbeCoords = clamp(baseProbeCoords + adjacentProbeOffset, 0, probeDim - 1);

            float3 trilinear = max(0.001f, lerp(1.f - alpha, alpha, adjacentProbeOffset));
            float1 trilinearWeight = 0.001f + (trilinear.x * trilinear.y * trilinear.z);

            const int adjacentPhysicsProbeLinearIndex = getPhysicalLinearVolumeId(adjacentProbeCoords);

            SH3_gi sample_world_gi_sh;
            {
                SH3_gi_pack sh_pack = BATL(SH3_gi_pack, sh_SRV, adjacentPhysicsProbeLinearIndex);
                sample_world_gi_sh.unpack(sh_pack);
            }

            if (sample_world_gi_sh.numSample < sampleClamp)
            {
                continue;
            }

            world_gi_sh.add(sample_world_gi_sh, trilinearWeight);
            weightAccumulation += trilinearWeight;
        }

        world_gi_sh.div(max(1e-6f, weightAccumulation));

        return weightAccumulation > 0.0;
    }

#endif
};
CHORD_CHECK_SIZE_GPU_SAFE(GIWorldProbeVolumeConfig);

struct GIScreenProbeSpawnInfo
{
    float3 normalRS;
    float  depth;

    uint2  pixelPosition;

#ifndef __cplusplus
    bool isValid()
    {
        return all(pixelPosition != 0);
    }

    void init()
    {
        depth = 0.0;
        normalRS = 0.0;
        pixelPosition = 0;
    }

    float3 getShootRayDir(uint2 gbufferDim, in const PerframeCameraView perView)
    {
        float2 uv = (pixelPosition + 0.5) / gbufferDim;
        const float4 viewPointCS = float4(screenUvToNdcUv(uv), kFarPlaneZ, 1.0);
        float4 viewPointRS = mul(perView.clipToTranslatedWorld, viewPointCS);
        return normalize(viewPointRS.xyz / viewPointRS.w);
    }

    float3 getProbePositionRS(uint2 gbufferDim, in const PerframeCameraView perView)
    {
        float2 uv = (pixelPosition + 0.5) / gbufferDim;

        if (depth < 0.0)
        {
            float t = -depth;
            float3 dir = getShootRayDir(gbufferDim, perView);

            return t * dir;
        }
        else
        {
            return getPositionRS(uv, depth, perView); 
        }
    }

    uint4 pack()
    {
        uint4 result;

        // 
        result.x = pixelPosition.x | (pixelPosition.y << 16u);
        
        //
        result.y = asuint(depth);

        float2 packUv = octahedralEncode(normalRS);
        result.z = asuint(packUv.x * 0.5 + 0.5);
        result.w = asuint(packUv.y * 0.5 + 0.5);

        return result;
    }

    void unpack(uint4 data)
    {
        pixelPosition.x = (data.x >>  0u) & 0xffffu;
        pixelPosition.y = (data.x >> 16u) & 0xffffu;

        //
        depth = asfloat(data.y);

        //
        float2 packUv;
        packUv.x = asfloat(data.z) * 2.0 - 1.0;
        packUv.y = asfloat(data.w) * 2.0 - 1.0;

        // 
        normalRS = octahedralDecode(packUv);
    }
#endif
};

#ifndef __cplusplus

// cell [0, 8] : group thread index.
// probe coord. 
float3 getScreenProbeCellRayDirection(const GPUBasicData scene, uint2 probeCoord, uint2 cell, float3 normal)
{
    // Noise 
    float2 blueNoise2 = STBN_float2(scene.blueNoiseCtx, probeCoord * 8 + cell, scene.frameCounter);
    float2 octUv = (cell + blueNoise2) / 8.0;

    // Hemisphere octahedral direction.
    const float3 texelDirection = hemiOctahedralDecode(octUv * 2.0 - 1.0);
    float3x3 tbn = createTBN(normal); 
    return tbnTransform(tbn, texelDirection);
}

#endif
