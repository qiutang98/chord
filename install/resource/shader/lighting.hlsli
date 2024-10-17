#pragma once 

#include "base.hlsli"

// Physical based lighting collections.
// http://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
// https://github.com/GPUOpen-Effects/FidelityFX-SSSR
// http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// https://www.cs.virginia.edu/~jdl/bib/appearance/analytic%20models/schlick94b.pdf

// PBR material info to evaluate shade.
struct PBRMaterial
{
    float3 diffuseColor; 
    // alphaRoughness = perceptualRoughness * perceptualRoughness;
    float alphaRoughness; 

    // perceptualRoughness is texture sample value.
    float perceptualRoughness; 




    float3 specularColor;  

    float3 reflectance0;
    float3 reflectance90;   

    float3 baseColor;
    float curvature;
};