#ifndef SHADER_MATERIAL_HLSLI
#define SHADER_MATERIAL_HLSLI

#include "nanite_shared.hlsli"

struct TinyGBufferContext
{
    float3 positionRS;

    // Lighting color. 
    float3 color;
    float3 emissivColor;

    // 
    float4 baseColor;

    // Relative worldspace normal. 
    float3 vertexNormalRS;
    float3 pixelNormalRS;

    // Motion vector. 
    float2 motionVector;

    // AO and alpha roughness and metallic.
    float materialAO;
    float roughness;
    float metallic;
};

void loadGLTFMetallicRoughnessPBRMaterial(
    in const GLTFMaterialGPUData materialInfo,
    in const TriangleMiscInfo triangleInfo,
    in const uint2 dispatchPos,
    in const float2 workTexelSize,
    out TinyGBufferContext gbufferCtx)
{
    // Screen uv. 
    const float2 screenUV = (dispatchPos + 0.5) * workTexelSize;

    //
    Barycentrics barycentricCtx = calculateTriangleBarycentrics(
        screenUvToNdcUv(screenUV), 
        triangleInfo.positionHS[0], 
        triangleInfo.positionHS[1], 
        triangleInfo.positionHS[2], 
        workTexelSize);

    const float3 barycentric = barycentricCtx.interpolation;
    const float3 ddx = barycentricCtx.ddx;
    const float3 ddy = barycentricCtx.ddy;

    float2 meshUv     = triangleInfo.uv[0] * barycentric.x + triangleInfo.uv[1] * barycentric.y + triangleInfo.uv[2] * barycentric.z;
    float2 meshUv_ddx = triangleInfo.uv[0] * ddx.x         + triangleInfo.uv[1] * ddx.y         + triangleInfo.uv[2] * ddx.z;
    float2 meshUv_ddy = triangleInfo.uv[0] * ddy.x         + triangleInfo.uv[1] * ddy.y         + triangleInfo.uv[2] * ddy.z;

    {
        float3 meshPositionHS = triangleInfo.positionHS_NoJitter[0] * barycentric.x + triangleInfo.positionHS_NoJitter[1] * barycentric.y + triangleInfo.positionHS_NoJitter[2] * barycentric.z;
        float3 meshPositionHS_LastFrame = triangleInfo.positionHS_NoJitter_LastFrame[0] * barycentric.x + triangleInfo.positionHS_NoJitter_LastFrame[1] * barycentric.y + triangleInfo.positionHS_NoJitter_LastFrame[2] * barycentric.z;
    
        gbufferCtx.motionVector = (meshPositionHS_LastFrame.xy / meshPositionHS_LastFrame.z - meshPositionHS.xy / meshPositionHS.z) * float2(0.5, -0.5);
    }

    //
    gbufferCtx.positionRS = triangleInfo.positionRS[0] * barycentric.x + triangleInfo.positionRS[1] * barycentric.y + triangleInfo.positionRS[2] * barycentric.z;

    float4 baseColor;
    {
        Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
        SamplerState baseColorSampler      = Bindless(SamplerState, materialInfo.baseColorSampler);
        baseColor = baseColorTexture.SampleGrad(baseColorSampler, meshUv, meshUv_ddx, meshUv_ddy) * materialInfo.baseColorFactor;
    }
    baseColor.xyz = mul(sRGB_2_AP1, baseColor.xyz);

    float3 emissiveColor = 0.0;
    {
        Texture2D<float4> emissiveTexture = TBindless(Texture2D, float4, materialInfo.emissiveTexture);
        SamplerState emissiveSampler = Bindless(SamplerState, materialInfo.emissiveSampler);
        emissiveColor = emissiveTexture.SampleGrad(emissiveSampler, meshUv, meshUv_ddx, meshUv_ddy).xyz * materialInfo.emissiveFactor;
    }
    gbufferCtx.color = 0.0;
    gbufferCtx.emissivColor = emissiveColor;

    // 
    gbufferCtx.baseColor = baseColor;

    // Vertex normal.
    // PERFORMANCE: normalize can remove here because we do barycentric lerp instead of scanline raster. 
    // const float3 vertNormalRS = normalize(triangleInfo.normalRS[0] * barycentric.x + triangleInfo.normalRS[1] * barycentric.y + triangleInfo.normalRS[2] * barycentric.z);
       const float3 vertNormalRS =          (triangleInfo.normalRS[0] * barycentric.x + triangleInfo.normalRS[1] * barycentric.y + triangleInfo.normalRS[2] * barycentric.z);
    
    float3 normalRS;

    const bool bExistNormalTexture = (materialInfo.normalTexture != kUnvalidIdUint32);
    [branch]
    if (bExistNormalTexture)
    {
        float3 tangentRS     =   triangleInfo.tangentRS[0] * barycentric.x +   triangleInfo.tangentRS[1] * barycentric.y +   triangleInfo.tangentRS[2] * barycentric.z;
        float3 bitangentRS   = triangleInfo.bitangentRS[0] * barycentric.x + triangleInfo.bitangentRS[1] * barycentric.y + triangleInfo.bitangentRS[2] * barycentric.z;

        const float3x3 TBN = float3x3(tangentRS, bitangentRS, vertNormalRS);

        Texture2D<float2> normalTexture = TBindless(Texture2D, float2, materialInfo.normalTexture);
        SamplerState normalSampler      = Bindless(SamplerState, materialInfo.normalSampler);

        float3 xyz;
        xyz.xy = normalTexture.SampleGrad(normalSampler, meshUv, meshUv_ddx, meshUv_ddy) * 2.0 - 1.0; // Remap to [-1, 1].
        xyz.z  = sqrt(1.0 - dot(xyz.xy, xyz.xy)); // Construct z.

        // Apply GLTF material normal scale.
        xyz.xy *= materialInfo.normalFactorScale;

        // Tangent space to world sapce.
        normalRS = mul(normalize(xyz), TBN);
    }
    else
    {
        normalRS = vertNormalRS;
    }
    gbufferCtx.vertexNormalRS = vertNormalRS;
    gbufferCtx.pixelNormalRS  = normalRS;

    const bool bExistAORoughnessMetallicTexture = (materialInfo.metallicRoughnessTexture != kUnvalidIdUint32);
    if (bExistAORoughnessMetallicTexture)
    {
        Texture2D<float4> metallicRoughnessTexture = TBindless(Texture2D, float4, materialInfo.metallicRoughnessTexture);
        SamplerState metallicRoughnessSampler      = Bindless(SamplerState, materialInfo.metallicRoughnessSampler);

        float4 metallicRoughnessRaw = metallicRoughnessTexture.SampleGrad(metallicRoughnessSampler, meshUv, meshUv_ddx, meshUv_ddy);

        //
        // NOTE: Some DCC store sqrt distribution roughness when export to keep higher precision when near zero. (called perceptual roughness). 
        //       BTW, we can't control the artist behavior (all media assets download from sketchfab), so, just set roughness is roughness. 

        // The metallic-roughness texture. The metalness values are sampled from the B channel. The roughness values are sampled from the G channel. 
        // These values **MUST** be encoded with a linear transfer function. 
        // If other channels are present (R or A), they **MUST** be ignored for metallic-roughness calculations. When undefined, the texture **MUST** be sampled as having `1.0` in G and B components.
        gbufferCtx.roughness =  metallicRoughnessRaw.g;

        //
        gbufferCtx.metallic  =  metallicRoughnessRaw.b;

        gbufferCtx.materialAO = (materialInfo.bExistOcclusion) 
            ? materialInfo.occlusionTextureStrength * metallicRoughnessRaw.r
            : 1.0f;
    }
    else
    {
        gbufferCtx.roughness = materialInfo.roughnessFactor;
        gbufferCtx.metallic  = getFallbackMetallic(materialInfo.metallicFactor);

        // Default no exist. 
        gbufferCtx.materialAO = 1.0f;
    }
}

#endif