// One pass hzb. 
#include "base.h"

struct HZBOnePushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(HZBOnePushConst);

    uint hzbView[kHZBMaxMipmapCount];
    uint sceneDepth;
    uint numWorkGroups;
    uint sceneDepthWidth;
    uint sceneDepthHeight;  
};
CHORD_PUSHCONST(HZBOnePushConst, pushConsts);

#define REDUCE_TYPE_MIN 0
#define REDUCE_TYPE_MAX 1

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"

// https://github.com/microsoft/DirectXShaderCompiler/issues/6880
[[vk::image_format("r16f")]]
[[vk::binding((int)chord::EBindingType::BindlessStorageImage, 0)]] 
RWTexture2D<float> RWTextureBindless[];

[[vk::image_format("r16f")]] 
[[vk::binding(0, 1)]] 
globallycoherent RWTexture2D<float> mip5Dest;

[[vk::binding(1, 1)]] 
globallycoherent RWStructuredBuffer<uint> counterBuffer;

groupshared uint sharedCounter;

void incrementCounter()
{
    InterlockedAdd(counterBuffer[0], 1, sharedCounter);
}

half4 loadSrcDepth4(uint2 posStripe2x2)
{
    half4 depth4;
    Texture2D<float> sceneDepth = TBindless(Texture2D, float, pushConsts.sceneDepth);

    uint2 edgeSamplePos = uint2(pushConsts.sceneDepthWidth - 1, pushConsts.sceneDepthHeight - 1);

#if REDUCE_TYPE == REDUCE_TYPE_MIN
    depth4.x = half(sceneDepth[min(posStripe2x2 + uint2(0, 0), edgeSamplePos)]);
    depth4.y = half(sceneDepth[min(posStripe2x2 + uint2(0, 1), edgeSamplePos)]);
    depth4.z = half(sceneDepth[min(posStripe2x2 + uint2(1, 0), edgeSamplePos)]);
    depth4.w = half(sceneDepth[min(posStripe2x2 + uint2(1, 1), edgeSamplePos)]);
#else 
    // Max value use ceil to quantify half
    depth4.x = half(f16tof32(f32tof16(sceneDepth[min(posStripe2x2 + uint2(0, 0), edgeSamplePos)]) + 1));
    depth4.y = half(f16tof32(f32tof16(sceneDepth[min(posStripe2x2 + uint2(0, 1), edgeSamplePos)]) + 1));
    depth4.z = half(f16tof32(f32tof16(sceneDepth[min(posStripe2x2 + uint2(1, 0), edgeSamplePos)]) + 1));
    depth4.w = half(f16tof32(f32tof16(sceneDepth[min(posStripe2x2 + uint2(1, 1), edgeSamplePos)]) + 1));
#endif 

    return depth4;
}

void loadHZBMip5Depth4(uint2 posStripe2x2, out half4 depth4)
{
    depth4.x = half(mip5Dest[posStripe2x2 + uint2(0, 0)]);
    depth4.y = half(mip5Dest[posStripe2x2 + uint2(0, 1)]);
    depth4.z = half(mip5Dest[posStripe2x2 + uint2(1, 0)]);
    depth4.w = half(mip5Dest[posStripe2x2 + uint2(1, 1)]);
}

void storeHZBMip5(half depth, uint2 storePos)
{
    mip5Dest[storePos] = depth;
}

void storeHZB(half depth, uint2 storePos, uint level)
{
    const uint rwId = pushConsts.hzbView[level];
    RWTextureBindless[rwId][storePos] = depth; // 
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
// >>>>>>>>>>>>>>>>>>>> LDS >>>>>>>>>>>>>>>>>>>>

groupshared half sharedValues[16][16];

void storeLDS(half v, uint x, uint y)
{
    sharedValues[x][y] = v;
}

// Return minMax.
half loadLDS(uint x, uint y) 
{
    return sharedValues[x][y];
}

half reductionFunc(half v0, half v1, half v2, half v3)
{
    half r;
#if REDUCE_TYPE == REDUCE_TYPE_MIN
    r = min(min(min(v0, v1), v2), v3);
#else
    r = max(max(max(v0, v1), v2), v3);
#endif
    return r;
}

// Return minMax.
half ldsReduction(uint2 p0, uint2 p1, uint2 p2, uint2 p3)
{
    half v0 = loadLDS(p0.x, p0.y);
    half v1 = loadLDS(p1.x, p1.y);
    half v2 = loadLDS(p2.x, p2.y);
    half v3 = loadLDS(p3.x, p3.y); 
    return reductionFunc(v0, v1, v2, v3);
}

half ldsReduction2x2(uint2 xy)
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
        half depths[4];

        // Mip #0. 16x16, each handle 2x2 tile, each tile exist 2x2 pixel.
        //                so process 64x64 pixels. total fill 32x32
        {
            const uint2 basicPos = workGroupId * 64 + xy * 2;
            const uint2 storePos = workGroupId * 32 + xy;

            // Mip 0 is 32x32
            [unroll(4)]
            for (int i = 0; i < 4; i++)
            {
                const uint2 offsetBasic = convert2d(i, 2);
                half4 depth4 = loadSrcDepth4(basicPos + 32 * offsetBasic); // src: 32x32 pertile

                #if REDUCE_TYPE == REDUCE_TYPE_MIN
                    depths[i] = min4(depth4);
                #else
                    depths[i] = max4(depth4);
                #endif

                storeHZB(depths[i], storePos + 16 * offsetBasic, 0); // mip0: 16x16 pertile
            }
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
                storeLDS(depths[i], xy.x, xy.y);
                GroupMemoryBarrierWithGroupSync();

                // Mip1 is 16x16
                if (localThreadIndex < 64)
                {
                    // 16x16 LDS reduce to 8x8
                    depths[i] = ldsReduction2x2(xy);

                    // Mip #1. 8x8 pertile.
                    storeHZB(depths[i], storePos + 8 * convert2d(i, 2), 1); 
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
            storeLDS(depths[0], xy.x + 0, xy.y + 0);
            storeLDS(depths[1], xy.x + 8, xy.y + 0);
            storeLDS(depths[2], xy.x + 0, xy.y + 8);
            storeLDS(depths[3], xy.x + 8, xy.y + 8);
        }
    }

    // Mip2: lds 16x16 -> 8x8
    {
        GroupMemoryBarrierWithGroupSync();

        if (localThreadIndex < 64)
        {
            half z = ldsReduction2x2(xy);
            storeHZB(z, workGroupId * 8 + xy, 2);
            storeLDS(z, xy.x, xy.y);
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
            half z = ldsReduction2x2(xy);
            storeHZB(z, workGroupId * 4 + xy, 3);
            storeLDS(z, xy.x, xy.y);
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
            half z = ldsReduction2x2(xy);
            storeHZB(z, workGroupId * 2 + xy, 4);
            storeLDS(z, xy.x, xy.y);
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
            half z = ldsReduction2x2(xy);
            storeHZBMip5(z, workGroupId);
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
        half depths[4];
        [unroll(4)]
        for (int i = 0; i < 4; i++)
        {
            const uint2 offsetBasic = convert2d(i, 2);

            half4 depth4;
            loadHZBMip5Depth4(basicPos + 2 * offsetBasic, depth4);

            #if REDUCE_TYPE == REDUCE_TYPE_MIN
                depths[i] = min4(depth4);
            #else
                depths[i] = max4(depth4);
            #endif
            storeHZB(depths[i], storePos + offsetBasic, 6);
        }

        #if MIP_COUNT <= 7
            return;
        #endif

        // Reduce 2x2
        half z = reductionFunc(depths[0], depths[1], depths[2], depths[3]);
        storeHZB(z, xy, 7);
        storeLDS(z, xy.x, xy.y); // 16x16
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
            half z = ldsReduction2x2(xy);
            storeHZB(z, xy, 8);
            storeLDS(z, xy.x, xy.y);
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
            half z = ldsReduction2x2(xy);
            storeHZB(z, xy, 9);
            storeLDS(z, xy.x, xy.y);
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
            half z = ldsReduction2x2(xy);
            storeHZB(z, xy, 10);
            storeLDS(z, xy.x, xy.y);
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
            half z = ldsReduction2x2(xy);
            storeHZB(z, 0, 11);
        }
    }

    #if MIP_COUNT > 12
        #error "Max support mip count is 11."
    #endif
}

#endif // !__cplusplus