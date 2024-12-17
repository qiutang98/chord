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

[numthreads(8, 8, 1)]
void mainCS(uint2 tid : SV_DispatchThreadID)
{
    const float2 uv = pushConsts.texelSize * (tid + 0.5);

    float NoV = uv.x;
    float roughness = uv.y;

    const float3 N  = float3(0.0, 0.0, 1.0); // Z up. 
    const float3 Up = float3(1.0, 0.0, 0.0);

    const float3 V = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV); // 

    float2 lut = 0.0;
    for (uint i = 0; i < kNumSample; i ++)
    {
        float2 xi = hammersley2d(i, kNumSample);

        // Sample direction build from importance sample GGX.
        float3 H = importanceSampleGGX(xi, roughness);

        // Tangent space

        float3 tangentX = normalize(cross(Up, N));
        float3 tangentY = normalize(cross(N, tangentX));

        // Convert to world Space
        H = normalize(tangentX * H.x + tangentY * H.y + N * H.z);

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

    storeRWTexture2D_float2(pushConsts.UAV, tid, lut / kNumSample);
}
#endif