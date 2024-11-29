#ifndef NOISE_HLSLI
#define NOISE_HLSLI

#include "bindless.hlsli"

// EGSR 2022: Spatiotemporal Blue Noise Masks, Wolfe et al.
// A Low-Discrepancy Sampler that Distributes Monte Carlo Errors as a Blue Noise in Screen Space, Heitz et al. (2019)
float samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d(
    in GPUBlueNoise noise, 
    uint pixel_i, 
    uint pixel_j, 
    uint sampleIndex, 
    uint sampleDimension)
{
    // wrap arguments
    pixel_i = pixel_i & 127u;
    pixel_j = pixel_j & 127u;
    sampleIndex = sampleIndex & 255u;
    sampleDimension = sampleDimension & 255u;

    // xor index based on optimized ranking
    uint rankedIndex = sampleDimension + (pixel_i + pixel_j * 128u) * 8u;
    uint rankedSampleIndex = sampleIndex ^ BATL(uint, noise.rankingTile, rankedIndex);

    // fetch value in sequence
    uint sobolIndex = sampleDimension + rankedSampleIndex * 256u;
    uint value = BATL(uint, noise.sobol, sobolIndex);

    // If the dimension is optimized, xor sequence value based on optimized scrambling
    uint scramblingIndex = (sampleDimension % 8) + (pixel_i + pixel_j * 128u) * 8u;
    value = value ^ BATL(uint, noise.scramblingTile, scramblingIndex);

    // convert to float and return
    float v = (0.5f + value) / 256.0f;

    return v;
}

// STBN 
// https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-1/
// https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-2/
// https://developer.nvidia.com/blog/rendering-in-real-time-with-spatiotemporal-blue-noise-textures-part-3/
float1 STBN_float1(in const GPUBlueNoiseContext ctx, uint2 screenPosition, uint frameIndex)
{
    Texture3D<float> r = TBindless(Texture3D, float, ctx.STBN_scalar);
    return r[int3(screenPosition, frameIndex) & int3(127, 127, 63)];
}

float2 STBN_float2(in const GPUBlueNoiseContext ctx, uint2 screenPosition, uint frameIndex)
{
    Texture3D<float2> r = TBindless(Texture3D, float2, ctx.STBN_vec2);
    return r[int3(screenPosition, frameIndex) & int3(127, 127, 63)];
}

float3 STBN_float3(in const GPUBlueNoiseContext ctx, uint2 screenPosition, uint frameIndex)
{
    Texture3D<float4> r = TBindless(Texture3D, float4, ctx.STBN_vec3);
    return r[int3(screenPosition, frameIndex) & int3(127, 127, 63)].xyz;
}

#endif