#include "base.h"

struct BRDFLutPushConsts
{
    float2 texelSize;
    uint UAV;
};
CHORD_PUSHCONST(BRDFLutPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "sample.hlsli" 
#include "bsdf.hlsli"
#include "base.hlsli"
#include "bindless.hlsli"

static const uint kNumSample = 1024;

#define kBatchSize 256
#define kLocalBatchSize (kNumSample / kBatchSize)
groupshared float2 sLut[kBatchSize];

[numthreads(kBatchSize, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    const float2 uv = pushConsts.texelSize * workGroupId;

    float NoV = uv.x;
    float roughness = uv.y;

    const float3 N = float3(0.0, 0.0, 1.0); // Z up. 
    const float3 V = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV); // 

    float2 lut = 0.0;
    for (uint i = localThreadIndex; i < kNumSample; i += kBatchSize)
    {
        float2 xi = hammersley2d(i, kNumSample);

        // Sample direction build from importance sample GGX.
        float3 H = importanceSampleGGX(xi, roughness);

        // Then compute light direction.
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(dot(N, L), 0.0);
		float VoH = max(dot(V, H), 0.0); 
		float NoH = max(dot(N, H), 0.0);

        if (NoL > 0.0) 
        {
            float G =  V_SmithGGXCorrelated(NoV, NoL, roughness);
            float Gv = 4.0 * NoL * G * VoH / NoH;
            float Fc = pow(1.0 - VoH, 5.0);

			lut += float2((1.0 - Fc) * Gv, Fc * Gv);
		}
    }

    lut /= kLocalBatchSize;

    sLut[localThreadIndex] = lut;
    GroupMemoryBarrierWithGroupSync();

    if (localThreadIndex < 128)
    {
        sLut[localThreadIndex] += sLut[localThreadIndex + 128];
    }
    GroupMemoryBarrierWithGroupSync();
    if (localThreadIndex < 64)
    {
        sLut[localThreadIndex] += sLut[localThreadIndex + 64];
    }
    GroupMemoryBarrierWithGroupSync();
    if (localThreadIndex < 32)
    {
        sLut[localThreadIndex] += sLut[localThreadIndex + 32];
    }
    GroupMemoryBarrierWithGroupSync();
    if (localThreadIndex < 16)
    {
        sLut[localThreadIndex] += sLut[localThreadIndex + 16];
    }
    if (localThreadIndex < 8)
    {
        sLut[localThreadIndex] += sLut[localThreadIndex + 8];
    }
    if (localThreadIndex < 4)
    {
        sLut[localThreadIndex] += sLut[localThreadIndex + 4];
    }
    if (localThreadIndex < 2)
    {
        sLut[localThreadIndex] += sLut[localThreadIndex + 2];
    }
    if (localThreadIndex < 1)
    {
        float2 finalLut = (sLut[0] + sLut[1]) / kBatchSize;
        storeRWTexture2D_float2(pushConsts.UAV, workGroupId, finalLut);
    }
}
#endif