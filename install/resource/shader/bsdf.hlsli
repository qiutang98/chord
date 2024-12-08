#ifndef SHADER_BSDF_HLSLI
#define SHADER_BSDF_HLSLI


// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from [4], Equation 15
float3 F_Schlick(float3 f0, float3 f90, float u) 
{
    return f0 + (f90 - f0) * pow(clamp(1.0 - u, 0.0, 1.0), 5.0);
}

// Fast approx of schlick fresnel. 
float3 F_SchlickFast(float3 f0, float u) 
{
    float f = pow(1.0 - u, 5.0);
    return f + f0 * (1.0 - f);
}




// Smith Joint GGX
// Note: Vis = G / (4 * NdotL * NdotV)
// see Eric Heitz. 2014. Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs. Journal of Computer Graphics Techniques, 3
// see Real-Time Rendering. Page 331 to 336.
// see https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg)
float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) 
{
    float a2 = roughness * roughness;

    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);

    float GGX = GGXV + GGXL;
    return (GGX > 0.0) ? (0.5 / GGX) : 0.0;
}

// Fast approximate for GGX.
float V_SmithGGXCorrelatedFast(float NoV, float NoL, float roughness) 
{
    float a = roughness;
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float D_GGX(float NoH, float roughness) 
{
    float a2 = roughness * roughness;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (kPI * f * f + 0.000001f);
}

// [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
// Adaptation to fit our G term.
float2 envBRDFApproxLazarov(float roughness, float NoV)
{
	const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
	const float4 c1 = { 1, 0.0425, 1.04, -0.04 };
	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;

	return float2(-1.04, 1.04) * a004 + r.zw;
}


#endif