#pragma once 

#include "base.hlsli"
#include "material.hlsli"
#include "raytrace_shared.hlsli"

// Physical based lighting collections.
// http://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
// https://github.com/GPUOpen-Effects/FidelityFX-SSSR
// http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// https://www.cs.virginia.edu/~jdl/bib/appearance/analytic%20models/schlick94b.pdf

struct ShadingResult
{
    float3 diffuseTerm;
    float3 specularTerm;

    void init()
    {
        diffuseTerm  = 0.0;
        specularTerm = 0.0;
    }

    void composite(in const ShadingResult other)
    {
        diffuseTerm  += other.diffuseTerm;
        specularTerm += other.specularTerm;
    }

    float3 color()
    {
        return diffuseTerm + specularTerm;
    }
};

// [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
// Adaptation to fit our G term.
float2 envBRDFApproxLazarov(float roughness, float NoV)
{
	const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
	const float4 c1 = { 1, 0.0425, 1.04, -0.04 };
	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
	return AB;
}

// PBR material info to evaluate shade.
struct PBRMaterial
{   
    // 
    uint lightingType;
    float3 diffuseColor; 

    float  roughness;
    float3 specularColor;  

    float3 reflectance0;
    float3 reflectance90;   

    void initGltfMetallicRoughnessPBR(in const TinyGBufferContext g)
    {
        lightingType  = kLightingType_GLTF_MetallicRoughnessPBR;

        //
        diffuseColor  = getDiffuseColor(g.baseColor.xyz, g.metallic);
        specularColor = getSpecularColor(g.baseColor.xyz, g.metallic);

        // 
        float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
        reflectance0 = specularColor;

        // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
        reflectance90 = clamp(reflectance * 50.0, 0.0, 1.0);

        roughness = g.roughness;
    }

    void initFromRayHitMaterialInfo(in const RayHitMaterialInfo g)
    {
       lightingType  = kLightingType_GLTF_MetallicRoughnessPBR;

        //
        diffuseColor  = getDiffuseColor(g.baseColor.xyz, g.metallic);
        specularColor = getSpecularColor(g.baseColor.xyz, g.metallic);

        // 
        float reflectance = max(max(specularColor.r, specularColor.g), specularColor.b);
        reflectance0 = specularColor;

        // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
        reflectance90 = clamp(reflectance * 50.0, 0.0, 1.0);

        roughness = g.roughness;
    }
};

// Light info mix.
struct AngularInfo
{
    float NdotL;  
    float NdotV;  
    float NdotH;  
    float LdotH; 
    float VdotH; 

    void init(float3 pointToLight, float3 normal, float3 view)
    {
        // Standard one-letter names
        float3 n = normalize(normal);           // Outward direction of surface point
        float3 v = normalize(view);             // Direction from surface point to view
        float3 l = normalize(pointToLight);     // Direction from surface point to light
        float3 h = normalize(l + v);            // Direction of the vector between l and v

        NdotL = clamp(dot(n, l), 0.0, 1.0);
        NdotV = clamp(dot(n, v), 0.0, 1.0);
        NdotH = clamp(dot(n, h), 0.0, 1.0);
        LdotH = clamp(dot(l, h), 0.0, 1.0);
        VdotH = clamp(dot(v, h), 0.0, 1.0);
    }
};

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

// Lambert lighting
// see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
float3 Fd_LambertDiffuse(PBRMaterial materialInfo)
{
    return materialInfo.diffuseColor / kPI;
}

// NOTE: Burley diffuse is expensive, and only add slightly image quality improvement.
//       We default use lambert diffuse. 
// Burley 2012, "Physically-Based Shading at Disney"
float3 Fd_BurleyDiffuse(PBRMaterial materialInfo, AngularInfo angularInfo) 
{
    float f90 = 0.5 + 2.0 * materialInfo.roughness * angularInfo.LdotH * angularInfo.LdotH;
    float3 lightScatter = F_Schlick(1.0, f90, angularInfo.NdotL);
    float3 viewScatter  = F_Schlick(1.0, f90, angularInfo.NdotV);
    return materialInfo.diffuseColor * lightScatter * viewScatter * (1.0 / kPI);
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

float visibilityOcclusion(PBRMaterial materialInfo, AngularInfo angularInfo)
{
    return V_SmithGGXCorrelated(angularInfo.NdotL, angularInfo.NdotV, materialInfo.roughness);
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

ShadingResult getPointShade_LightingType_GLTF_MetallicRoughnessPBR(float3 pointToLight, PBRMaterial materialInfo, float3 normal, float3 view)
{
    ShadingResult result;
    result.init();

    AngularInfo angularInfo;
    angularInfo.init(pointToLight, normal, view);

    if (materialInfo.lightingType == kLightingType_GLTF_MetallicRoughnessPBR)
    {
        // Skip unorientation to light pixels.
        if (angularInfo.NdotL > 0.0 || angularInfo.NdotV > 0.0)
        {
            // Calculate the shading terms for the microfacet specular shading model
            float3  F = F_Schlick(materialInfo.reflectance0, materialInfo.reflectance90, angularInfo.VdotH);
            float Vis = V_SmithGGXCorrelated(angularInfo.NdotL, angularInfo.NdotV, materialInfo.roughness);
            float D   = D_GGX(angularInfo.NdotH, materialInfo.roughness);

            // Calculation of analytical lighting contribution
            float3 diffuseContrib = (1.0 - F) * Fd_LambertDiffuse(materialInfo);
            float3 specContrib    = F * Vis * D;

            // 
            result.diffuseTerm  = angularInfo.NdotL * diffuseContrib;
            result.specularTerm = angularInfo.NdotL * specContrib;
        }
    }

    return result;
}

// Directional light direct lighting evaluate.
ShadingResult evaluateDirectionalDirectLight_LightingType_GLTF_MetallicRoughnessPBR(
    float3 direction, 
    float3 radiance,
    PBRMaterial materialInfo, 
    float3 normal, 
    float3 pixelToCamera)
{
    // Directional lighting direction is light pos to camera pos normalize vector.
    // So need inverse here for point to light.
    float3 pointToLight = -direction;
    float3 view = -pixelToCamera;

    ShadingResult shade = getPointShade_LightingType_GLTF_MetallicRoughnessPBR(pointToLight, materialInfo, normal, view);
    shade.diffuseTerm  *= radiance;
    shade.specularTerm *= radiance;

    return shade;
}