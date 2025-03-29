#ifndef SHADER_HZB_HLSL
#define SHADER_HZB_HLSL

// One pass hzb. 
#include "base.h"

struct HZBPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(HZBPushConst);

    uint hzbMinView[kHZBMaxMipmapCount];
    uint hzbMaxView[kHZBMaxMipmapCount];
    uint sceneDepth;
    uint numWorkGroups;
    uint sceneDepthWidth;
    uint sceneDepthHeight;  

    // 
    uint validDepthMinMaxBufferId;
};
CHORD_PUSHCONST(HZBPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"

[[vk::binding(0, 1)]] globallycoherent RWTexture2D<float> mip5DestMin;
[[vk::binding(1, 1)]] globallycoherent RWTexture2D<float> mip5DestMax;
[[vk::binding(2, 1)]] globallycoherent RWStructuredBuffer<uint> counterBuffer;

groupshared uint sharedCounter;

void incrementCounter()
{
    InterlockedAdd(counterBuffer[0], 1, sharedCounter);
}

float4 loadSrcDepth4(uint2 posStripe2x2)
{
    float4 depth4;
    Texture2D<float> sceneDepth = TBindless(Texture2D, float, pushConsts.sceneDepth);

    uint2 edgeSamplePos = uint2(pushConsts.sceneDepthWidth - 1, pushConsts.sceneDepthHeight - 1);

    depth4.x = sceneDepth[min(posStripe2x2 + uint2(0, 0), edgeSamplePos)];
    depth4.y = sceneDepth[min(posStripe2x2 + uint2(0, 1), edgeSamplePos)];
    depth4.z = sceneDepth[min(posStripe2x2 + uint2(1, 0), edgeSamplePos)];
    depth4.w = sceneDepth[min(posStripe2x2 + uint2(1, 1), edgeSamplePos)];

    return depth4;
}

void loadHZBMip5Depth4(uint2 posStripe2x2, out float4 minDepth4, out float4 maxDepth4)
{
    minDepth4.x = mip5DestMin[posStripe2x2 + uint2(0, 0)];
    minDepth4.y = mip5DestMin[posStripe2x2 + uint2(0, 1)];
    minDepth4.z = mip5DestMin[posStripe2x2 + uint2(1, 0)];
    minDepth4.w = mip5DestMin[posStripe2x2 + uint2(1, 1)];

    maxDepth4.x = mip5DestMax[posStripe2x2 + uint2(0, 0)];
    maxDepth4.y = mip5DestMax[posStripe2x2 + uint2(0, 1)];
    maxDepth4.z = mip5DestMax[posStripe2x2 + uint2(1, 0)];
    maxDepth4.w = mip5DestMax[posStripe2x2 + uint2(1, 1)];
}

void storeHZBMip5(float2 depthMinMax, uint2 storePos)
{
    mip5DestMin[storePos] = depthMinMax.x; // Min value default store, floor default.
    mip5DestMax[storePos] = f16tof32(f32tof16(depthMinMax.y) + 1); // Max value use ceil. 
}

void storeHZB(float2 depthMinMax, uint2 storePos, uint level)
{
    RWTexture2D<float> mipDestMin = TBindless(RWTexture2D, float, pushConsts.hzbMinView[level]);
    RWTexture2D<float> mipDestMax = TBindless(RWTexture2D, float, pushConsts.hzbMaxView[level]);

    mipDestMin[storePos] = depthMinMax.x; // 
    mipDestMax[storePos] = depthMinMax.y; // Max de
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// >>>>>>>>>>>>>>>>>>>> LDS >>>>>>>>>>>>>>>>>>>>

groupshared float sharedValuesR[16][16]; // Min
groupshared float sharedValuesG[16][16]; // Max

void storeLDS(float2 minMax, uint x, uint y)
{
    sharedValuesR[x][y] = minMax.r;
    sharedValuesG[x][y] = minMax.g;
}

// Return minMax.
float2 loadLDS(uint x, uint y) 
{
    return float2(sharedValuesR[x][y], sharedValuesG[x][y]);
}

float2 reductionFunc(float2 v0, float2 v1, float2 v2, float2 v3)
{
    float2 r;
    r.x = min(min(min(v0.x, v1.x), v2.x), v3.x);
    r.y = max(max(max(v0.y, v1.y), v2.y), v3.y);
    return r;
}

// Return minMax.
float2 ldsReduction(uint2 p0, uint2 p1, uint2 p2, uint2 p3)
{
    float2 v0 = loadLDS(p0.x, p0.y);
    float2 v1 = loadLDS(p1.x, p1.y);
    float2 v2 = loadLDS(p2.x, p2.y);
    float2 v3 = loadLDS(p3.x, p3.y); 
    return reductionFunc(v0, v1, v2, v3);
}

float2 ldsReduction2x2(uint2 xy)
{
    return ldsReduction(xy * 2, xy * 2 + uint2(1, 0), xy * 2 + uint2(0, 1), xy * 2 + 1);
}

// <<<<<<<<<<<<<<<<<<<< LDS <<<<<<<<<<<<<<<<<<<<
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

// Min backbuffer size is 64x64, so mip min count is 6.
[numthreads(256, 1, 1)]
void mainCS(uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    uint2 xy = remap16x16(localThreadIndex);

    // 0 - 1
    {
        float2 depthMinMaxs[4];

        // Mip #0. 16x16, each handle 2x2 tile, each tile exist 2x2 pixel.
        //                so process 64x64 pixels. total fill 32x32
        {
            const uint2 basicPos = workGroupId * 64 + xy * 2;
            const uint2 storePos = workGroupId * 32 + xy;

        #if DIM_COMPUTE_VALID_RANGE
            float zValidMax = -10.0;
            float zValidMin =  10.0;
        #endif

            // Mip 0 is 32x32
            [unroll(4)]
            for (int i = 0; i < 4; i++)
            {
                const uint2 offsetBasic = convert2d(i, 2);
                float4 depth4 = loadSrcDepth4(basicPos + 32 * offsetBasic); // src: 32x32 pertile
                depthMinMaxs[i] = float2(min4(depth4), max4(depth4));
                storeHZB(depthMinMaxs[i], storePos + 16 * offsetBasic, 0); // mip0: 16x16 pertile

            #if DIM_COMPUTE_VALID_RANGE
                [flatten] if (depth4.x > 0.0) { zValidMax = max(zValidMax, depth4.x); zValidMin = min(zValidMin, depth4.x); }
                [flatten] if (depth4.y > 0.0) { zValidMax = max(zValidMax, depth4.y); zValidMin = min(zValidMin, depth4.y); }
                [flatten] if (depth4.z > 0.0) { zValidMax = max(zValidMax, depth4.z); zValidMin = min(zValidMin, depth4.z); }
                [flatten] if (depth4.w > 0.0) { zValidMax = max(zValidMax, depth4.w); zValidMin = min(zValidMin, depth4.w); }
            #endif
            }

        #if DIM_COMPUTE_VALID_RANGE
            float zValidMaxInWave = WaveActiveMax(zValidMax);
            float zValidMinInWave = WaveActiveMin(zValidMin);
            if (WaveIsFirstLane())
            {
                RWByteAddressBuffer bufferAcess = RWByteAddressBindless(pushConsts.validDepthMinMaxBufferId);
                if (zValidMinInWave < 1.0) { uint o; bufferAcess.InterlockedMin(0, asuint(zValidMinInWave), o);  }
                if (zValidMaxInWave > 0.0) { uint o; bufferAcess.InterlockedMax(4, asuint(zValidMaxInWave), o);  }
            }
        #endif
        }

        #if MIP_COUNT <= 1
            return;
        #endif

        // 2x2 Tile, each tile is 8 * 8, total fill 16x16
        {
            const uint2 storePos = workGroupId * 16 + xy;

            [unroll(4)]
            for (int i = 0; i < 4; i++)
            {
                storeLDS(depthMinMaxs[i], xy.x, xy.y);
                GroupMemoryBarrierWithGroupSync();

                // Mip1 is 16x16
                if (localThreadIndex < 64)
                {
                    // 16x16 LDS reduce to 8x8
                    depthMinMaxs[i] = ldsReduction2x2(xy);

                    // Mip #1. 8x8 pertile.
                    storeHZB(depthMinMaxs[i], storePos + 8 * convert2d(i, 2), 1); 
                }
                GroupMemoryBarrierWithGroupSync();
            }
        }

        #if MIP_COUNT <= 2
            return;
        #endif

        // Mip #1 LDS fill: 16x16
        if (localThreadIndex < 64)
        {
            storeLDS(depthMinMaxs[0], xy.x + 0, xy.y + 0);
            storeLDS(depthMinMaxs[1], xy.x + 8, xy.y + 0);
            storeLDS(depthMinMaxs[2], xy.x + 0, xy.y + 8);
            storeLDS(depthMinMaxs[3], xy.x + 8, xy.y + 8);
        }
    }

    // Mip2: lds 16x16 -> 8x8
    {
        GroupMemoryBarrierWithGroupSync();

        if (localThreadIndex < 64)
        {
            float2 minMax = ldsReduction2x2(xy);
            storeHZB(minMax, workGroupId * 8 + xy, 2);
            storeLDS(minMax, xy.x, xy.y);
        }
    }

    #if MIP_COUNT <= 3
        return;
    #endif

    // 3: 4x4
    {
        GroupMemoryBarrierWithGroupSync();

        if (localThreadIndex < 16)
        {
            float2 minMax = ldsReduction2x2(xy);
            storeHZB(minMax, workGroupId * 4 + xy, 3);
            storeLDS(minMax, xy.x, xy.y);
        }
    }

    #if MIP_COUNT <= 4
        return;
    #endif

    // 4: 2x2
    {
        GroupMemoryBarrierWithGroupSync();
        if (localThreadIndex < 4)
        {
            float2 minMax = ldsReduction2x2(xy);
            storeHZB(minMax, workGroupId * 2 + xy, 4);
            storeLDS(minMax, xy.x, xy.y);
        }
    }

    #if MIP_COUNT <= 5
        return;
    #endif

    // 5: 1x1
    {
        GroupMemoryBarrierWithGroupSync();
        if (localThreadIndex < 1)
        {
            float2 minMax = ldsReduction2x2(xy);
            storeHZBMip5(minMax, workGroupId);
        }
    }

    #if MIP_COUNT <= 6
        return;
    #endif

    if (localThreadIndex == 0)
    {
        incrementCounter();
    }
    GroupMemoryBarrierWithGroupSync();

    // Kill all threadGroup excluded last one. 
    if (sharedCounter != pushConsts.numWorkGroups - 1)
    {
        return;
    }

    // 6-7
    {
        const uint2 basicPos = xy * 4;
        const uint2 storePos = xy * 2;

        // Mip 6: 32x32
        float2 depthMinMaxs[4];
        [unroll(4)]
        for (int i = 0; i < 4; i++)
        {
            const uint2 offsetBasic = convert2d(i, 2);

            float4 depth4Min;
            float4 depth4Max;
            loadHZBMip5Depth4(basicPos + 2 * offsetBasic, depth4Min, depth4Max);

            depthMinMaxs[i] = float2(min4(depth4Min), max4(depth4Max));
            storeHZB(depthMinMaxs[i], storePos + offsetBasic, 6);
        }

        #if MIP_COUNT <= 7
            return;
        #endif

        // Reduce 2x2
        float2 minMax = reductionFunc(depthMinMaxs[0], depthMinMaxs[1], depthMinMaxs[2], depthMinMaxs[3]);
        storeHZB(minMax, xy, 7);
        storeLDS(minMax, xy.x, xy.y); // 16x16
    }

    #if MIP_COUNT <= 8
        return;
    #endif

    // 8
    {
        // 8x8 tile.
        GroupMemoryBarrierWithGroupSync();

        if (localThreadIndex < 64)
        {
            float2 minMax = ldsReduction2x2(xy);
            storeHZB(minMax, xy, 8);
            storeLDS(minMax, xy.x, xy.y);
        }
    }

    #if MIP_COUNT <= 9
        return;
    #endif

    // 9
    {
        GroupMemoryBarrierWithGroupSync();

        if (localThreadIndex < 16)
        {
            // 4x4 tile
            float2 minMax = ldsReduction2x2(xy);
            storeHZB(minMax, xy, 9);
            storeLDS(minMax, xy.x, xy.y);
        }
    }

    #if MIP_COUNT <= 10
        return;
    #endif


    // 10
    {
        GroupMemoryBarrierWithGroupSync();
        // 2x2 tile
        if (localThreadIndex < 4)
        {
            float2 minMax = ldsReduction2x2(xy);
            storeHZB(minMax, xy, 10);
            storeLDS(minMax, xy.x, xy.y);
        }
    }

    #if MIP_COUNT <= 11
        return;
    #endif

    // 11
    {
        // 1x1 tile
        GroupMemoryBarrierWithGroupSync();
        if (localThreadIndex < 1)
        {
            float2 minMax = ldsReduction2x2(xy);
            storeHZB(minMax, 0, 11);
        }
    }

    #if MIP_COUNT > 12
        #error "Max support mip count is 11."
    #endif
}

#endif // !__cplusplus

#endif // SHADER_HZB_HLSL