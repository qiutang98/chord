#include "gi.h"

struct GIUpsamplePushConsts
{
    uint2 fullDim;
    uint2 lowDim;

    uint cameraViewId; 

    uint full_depthSRV;
    uint full_normalSRV;
    uint low_depthSRV;
    uint low_normalSRV;

    uint UAV;

    uint SRV;
    uint SRV_1;

    uint baseColorId;
    uint aoRoughnessMetallicId;
    float diffuseGIScale;
    float specularGIScale;
};
CHORD_PUSHCONST(GIUpsamplePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bsdf.hlsli"

float normalEdgeStoppingWeight(float3 centerNormal, float3 sampleNormal, float power)
{
    return pow(clamp(dot(centerNormal, sampleNormal), 0.0f, 1.0f), power);
}

float depthEdgeStoppingWeight(float centerDepth, float sampleDepth, float phi)
{
    return exp(-abs(centerDepth - sampleDepth) / phi);
}

float edgeStoppingWeight(float centerDepth, float sampleDepth, float phi_z, float3 centerNormal, float3 sampleNormal, float phi_normal)
{
    const float wZ      = depthEdgeStoppingWeight(centerDepth, sampleDepth, phi_z);   
    const float wNormal = normalEdgeStoppingWeight(centerNormal, sampleNormal, phi_normal);

    const float wL = 1.0f;
    return exp(0.0 - max(wL, 0.0) - max(wZ, 0.0)) * wNormal;
}


[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const int2 gid = remap8x8(localThreadIndex); 
    const int2 tid = workGroupId * 8 + gid;

    if (any(tid >= pushConsts.fullDim))
    {
        // Out of bound pre-return.  
        return; 
    }

    // 
    float fullRes_deviceZ =  loadTexture2D_float1(pushConsts.full_depthSRV, tid);
    if (fullRes_deviceZ <= 0.0)
    {
        return;
    }

    float2 fullRes_texelSize = 1.0 / pushConsts.fullDim;
    float2 lowRes_texelSize  = 1.0 / pushConsts.lowDim;

    // 
    float2 uv = (tid + 0.5) * fullRes_texelSize;

    // 
    const float3 fullRes_positionRS = getPositionRS(uv, max(fullRes_deviceZ, kFloatEpsilon), perView); 

    float3 fullRes_normalRS = loadTexture2D_float4(pushConsts.full_normalSRV, tid).xyz * 2.0 - 1.0;
    fullRes_normalRS = normalize(fullRes_normalRS);



    SamplerState pointSampler = getPointClampEdgeSampler(perView);


    float3 baseColor = loadTexture2D_float3(pushConsts.baseColorId, tid);
    float4 aoRoughnessMetallic = loadTexture2D_float4(pushConsts.aoRoughnessMetallicId, tid);

    // 

    float materialAo = aoRoughnessMetallic.x;
    float roughness  = aoRoughnessMetallic.y;
    float metallic   = aoRoughnessMetallic.z;

    float3 radiance = 0.0;
    float weightSum = 0.0;

    float3 radiance_1 = 0.0;
    float weightSum_1 = 0.0;

    const float kWeights_0[3] = { 1.0, 1.0 / 2.0, 1.0 / 3.0 };
    const float kWeights_1[3] = { 1.0, 1.0 / 8.0, 1.0 / 16.0 };
    for(int x = -1; x <= 1; x ++)
    {
        for (int y = -1; y <= 1; y ++)
        {
            float2 lowRes_uv = uv + int2(x, y) * lowRes_texelSize;
            float lowRes_depth = sampleTexture2D_float1(pushConsts.low_depthSRV, lowRes_uv, pointSampler);

            if (lowRes_depth <= 0.0)
            {
                continue; // Sky skip. 
            }

            float3 lowRes_normalRS = sampleTexture2D_float4(pushConsts.low_normalSRV, lowRes_uv, pointSampler).xyz * 2.0 - 1.0;
            lowRes_normalRS = normalize(lowRes_normalRS);

            float w = edgeStoppingWeight(fullRes_deviceZ, lowRes_depth, 1.0f, fullRes_normalRS, lowRes_normalRS, 32.0f);
            weightSum += w;
            radiance   += sampleTexture2D_float4(pushConsts.SRV, lowRes_uv, pointSampler).xyz * w;

            float roughWeight = kWeights_0[abs(x)] * kWeights_0[abs(y)];
            float glossyWeight = kWeights_1[abs(x)] * kWeights_1[abs(y)];
           
            w *= lerp(glossyWeight, roughWeight, roughness);
            radiance_1  += sampleTexture2D_float4(pushConsts.SRV_1, lowRes_uv, pointSampler).xyz * w;
            weightSum_1 += w;
        }
    }

    radiance = (weightSum < 1e-6f) ? 0.0 : radiance / weightSum;


    float3 radiance_specular = (weightSum_1 < 1e-6f) ? 0.0 : radiance_1 / weightSum_1;


    // 
    float3 srcColor = loadRWTexture2D_float3(pushConsts.UAV, tid);



    //
    float3 diffuseColor  = pushConsts.diffuseGIScale * getDiffuseColor(baseColor, metallic);
    float3 specularColor = pushConsts.specularGIScale * getSpecularColor(baseColor, metallic);

    // Apply diffuse gi. 
    radiance = srcColor + radiance * diffuseColor;

    // Apply specular gi. 
    {
        float3 V = normalize(-fullRes_positionRS);
        float NoV = saturate(dot(fullRes_normalRS, V));

        float2 brdf = sampleBRDFLut(perView, NoV, sqrt(roughness)); // sqrt remap Reflection distribution.
        // float2 brdf = envBRDFApproxLazarov(sqrt(roughness), NoV); 
        radiance += radiance_specular * (specularColor * brdf.x + brdf.y);  
    } 

    storeRWTexture2D_float3(pushConsts.UAV, tid, radiance); 
} 


#endif 