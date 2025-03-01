#ifndef NOISE_HLSLI
#define NOISE_HLSLI

#include "bindless.hlsli"

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