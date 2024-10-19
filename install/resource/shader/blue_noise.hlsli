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

#endif