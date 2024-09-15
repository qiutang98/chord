#include "base.h"

struct AtmospherePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(AtmospherePushConsts);

    uint constBufferId;
    uint linearSampler;

    uint flag0;
    uint flag1;
    uint flag2;
    uint flag3;

    uint srv0;
    uint srv1;
    uint srv2; 
    uint srv3;
    uint srv4;

    uint uav0;
    uint uav1;
    uint uav2; 
    uint uav3;
};
CHORD_PUSHCONST(AtmospherePushConsts, pushConsts);

// Provide linear sampler for used.
#define DISABLE_ATMOSPHERE_CHECK
#define ATMOSPHERE_LINEAR_SAMPLER Bindless(SamplerState, pushConsts.linearSampler)
#include "atmosphere.hlsli"

struct AtmospherePrecomputeParameter
{
    AtmosphereParameters atmosphere;
    float4 luminanceFromRadiance_0;
    float4 luminanceFromRadiance_1;
    float4 luminanceFromRadiance_2;
};

#ifndef __cplusplus // HLSL only area.

float3 RadianceToLuminance(float3 radiance, in const AtmospherePrecomputeParameter params)
{
    float3x3 luminanceFromRadiance = 
    {
        params.luminanceFromRadiance_0.xyz,
        params.luminanceFromRadiance_1.xyz,
        params.luminanceFromRadiance_2.xyz,
    };

    return mul(luminanceFromRadiance, radiance);
}

[numthreads(8, 8, 1)]
void transmittanceLutCS(uint2 workPos : SV_DispatchThreadID)
{
    const AtmospherePrecomputeParameter params = BATL(AtmospherePrecomputeParameter, pushConsts.constBufferId, 0);
    const float2 fragCoord = workPos + 0.5;

    // 
    RWTexture2D<float4> rwTransmittance = TBindless(RWTexture2D, float4, pushConsts.uav0);
    rwTransmittance[workPos] = float4(ComputeTransmittanceToTopAtmosphereBoundaryTexture(params.atmosphere, fragCoord), 1.0);
} 

[numthreads(8, 8, 1)]
void directIrradianceCS(uint2 workPos : SV_DispatchThreadID)
{
    const AtmospherePrecomputeParameter params = BATL(AtmospherePrecomputeParameter, pushConsts.constBufferId, 0);
    const float2 fragCoord = workPos + 0.5;

    Texture2D<float4> transmittanceTexture = TBindless(Texture2D,   float4, pushConsts.srv0);
    RWTexture2D<float4> rwDeltaIrradiance  = TBindless(RWTexture2D, float4, pushConsts.uav0);

    //
    rwDeltaIrradiance[workPos] = float4(ComputeDirectIrradianceTexture(params.atmosphere, transmittanceTexture, fragCoord), 1.0);

    // Clear.
    if (pushConsts.flag0 == 1)
    {
        RWTexture2D<float4> rwIrradiance = TBindless(RWTexture2D, float4, pushConsts.uav1);
        rwIrradiance[workPos] = 0;
    }
}

[numthreads(8, 8, 1)]
void singleScatteringCS(uint3 workPos : SV_DispatchThreadID)
{
    const AtmospherePrecomputeParameter params = BATL(AtmospherePrecomputeParameter, pushConsts.constBufferId, 0);
    const float3 fragCoord = workPos + 0.5;

    Texture2D<float4> transmittanceTexture = TBindless(Texture2D,   float4, pushConsts.srv0);

    float3 deltaRayleigh, deltaMie;
    ComputeSingleScatteringTexture(params.atmosphere, transmittanceTexture, fragCoord, deltaRayleigh, deltaMie);

    float4 scattering = float4(RadianceToLuminance(deltaRayleigh, params), RadianceToLuminance(deltaMie, params).r);

    RWTexture3D<float4> rwDeltaRayleigh = TBindless(RWTexture3D, float4, pushConsts.uav0);
    RWTexture3D<float4> rwDeltaMie = TBindless(RWTexture3D, float4, pushConsts.uav1);
    RWTexture3D<float4> rwScattering = TBindless(RWTexture3D, float4, pushConsts.uav2);
    rwDeltaRayleigh[workPos] = float4(deltaRayleigh, 1.0);
    rwDeltaMie[workPos] = float4(deltaMie, 1.0);


    // Accumulate.
    if (pushConsts.flag1 == 1)
    {
        rwScattering[workPos] += scattering;
    }
    else
    {
        rwScattering[workPos] = scattering;
    }

    // Exist single mie scattering.
    if (pushConsts.flag0 == 1)
    {
        float3 singleMieScattering = RadianceToLuminance(deltaMie, params);

        RWTexture3D<float4> rwSingleMieScattering = TBindless(RWTexture3D, float4, pushConsts.uav3);
        // Accumulate.
        if (pushConsts.flag1 == 1)
        {
            rwSingleMieScattering[workPos] += float4(singleMieScattering, 1.0);
        }
        else
        {
            rwSingleMieScattering[workPos] = float4(singleMieScattering, 1.0);
        }

    }
}

[numthreads(8, 8, 1)]
void scatteringDensityCS(uint3 workPos : SV_DispatchThreadID)
{
    const AtmospherePrecomputeParameter params = BATL(AtmospherePrecomputeParameter, pushConsts.constBufferId, 0);
    const float3 fragCoord = workPos + 0.5;

    Texture2D<float4> transmittanceTexture            = TBindless(Texture2D,   float4, pushConsts.srv0);
    Texture3D<float4> singleRayleighScatteringTexture = TBindless(Texture3D,   float4, pushConsts.srv1);
    Texture3D<float4> singleMieScatteringTexture      = TBindless(Texture3D,   float4, pushConsts.srv2);
    Texture3D<float4> multipleScatteringTexture       = TBindless(Texture3D,   float4, pushConsts.srv3);
    Texture2D<float4> irradianceTexture               = TBindless(Texture2D,   float4, pushConsts.srv4);

    float3 scatteringDensity = ComputeScatteringDensityTexture(
        params.atmosphere, 
        transmittanceTexture, 
        singleRayleighScatteringTexture,
        singleMieScatteringTexture, 
        multipleScatteringTexture,
        irradianceTexture, 
        fragCoord, 
        pushConsts.flag0); // scatteringOrder

    RWTexture3D<float4> rwScatteringDensity = TBindless(RWTexture3D, float4, pushConsts.uav0);
    rwScatteringDensity[workPos] = float4(scatteringDensity, 1.0);
}

[numthreads(8, 8, 1)]
void indirectIrradianceCS(uint2 workPos : SV_DispatchThreadID)
{
    const AtmospherePrecomputeParameter params = BATL(AtmospherePrecomputeParameter, pushConsts.constBufferId, 0);
    const float2 fragCoord = workPos + 0.5;

    Texture3D<float4> singleRayleighScatteringTexture = TBindless(Texture3D,   float4, pushConsts.srv0);
    Texture3D<float4> singleMieScatteringTexture      = TBindless(Texture3D,   float4, pushConsts.srv1);
    Texture3D<float4> multipleScatteringTexture       = TBindless(Texture3D,   float4, pushConsts.srv2);

    float3 deltaIrradiance = ComputeIndirectIrradianceTexture(
        params.atmosphere, 
        singleRayleighScatteringTexture,
        singleMieScatteringTexture,
        multipleScatteringTexture,
        fragCoord, 
        pushConsts.flag0); // scatteringOrder


    RWTexture2D<float4> rwDeltaIrradiance = TBindless(RWTexture2D, float4, pushConsts.uav1);
    rwDeltaIrradiance[workPos] = float4(deltaIrradiance, 1.0);

    RWTexture2D<float4> rwIrradiance = TBindless(RWTexture2D, float4, pushConsts.uav0);
    float4 lumiance = float4(RadianceToLuminance(deltaIrradiance, params), 1.0);

    // Accumulate.
    rwIrradiance[workPos] += lumiance; 
}

[numthreads(8, 8, 1)]
void multipleScatteringCS(uint3 workPos : SV_DispatchThreadID)
{
    const AtmospherePrecomputeParameter params = BATL(AtmospherePrecomputeParameter, pushConsts.constBufferId, 0);
    const float3 fragCoord = workPos + 0.5;

    Texture2D<float4> transmittanceTexture  = TBindless(Texture2D, float4, pushConsts.srv0);
    Texture3D<float4> scatterDensityTexture = TBindless(Texture3D, float4, pushConsts.srv1);

    float nu;
    float3 deltaMultipleScattering = ComputeMultipleScatteringTexture(
        params.atmosphere, 
        transmittanceTexture,
        scatterDensityTexture,
        fragCoord,
        nu);

    float4 scattering = float4(RadianceToLuminance(deltaMultipleScattering, params) / rayleighPhase(nu), 0.0);

    RWTexture3D<float4> rwDeltaMultiScattering = TBindless(RWTexture3D, float4, pushConsts.uav0);
    rwDeltaMultiScattering[workPos] = float4(deltaMultipleScattering, 1.0);

    RWTexture3D<float4> rwScattering = TBindless(RWTexture3D, float4, pushConsts.uav1);

    // Accumulate.
    rwScattering[workPos] += scattering;
}
#endif  