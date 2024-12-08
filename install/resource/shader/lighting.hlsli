#ifndef SHADER_LIGHTING_HLSLI
#define SHADER_LIGHTING_HLSLI

#include "base.hlsli"
#include "material.hlsli"
#include "raytrace_shared.hlsli"
#include "bsdf.hlsli"

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

float visibilityOcclusion(PBRMaterial materialInfo, AngularInfo angularInfo)
{
    return V_SmithGGXCorrelated(angularInfo.NdotL, angularInfo.NdotV, materialInfo.roughness);
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

#endif