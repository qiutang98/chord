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



struct GIScreenProbeSpawnInfo
{
    float3 normalRS;
    float  depth;
    uint2  pixelPosition;

#ifndef __cplusplus
    bool isValid()
    {
        return depth > 0.0;
    }

    void init()
    {
        depth = 0.0;
        normalRS = 0.0;
        pixelPosition = 0;
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
