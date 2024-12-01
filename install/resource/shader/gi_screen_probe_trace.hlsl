#include "gi.h"

struct GIScreenProbeTracePushConsts
{
    uint2 probeDim;
    uint2 gbufferDim;

    uint cascadeCount;
    uint shadowViewId;
    uint shadowDepthIds;
    uint transmittanceId;

    uint scatteringId;
    uint singleMieScatteringId;
    uint linearSampler;
    uint irradianceTextureId;

    float minRayTraceDistance;
    float maxRayTraceDistance;
    uint cameraViewId;
    uint probeSpawnInfoSRV;

    uint radianceUAV;
    float rayHitLODOffset;

    bool bHistoryValid;
    uint clipmapConfigBufferId;
    uint clipmapCount; // 2 4 8
};
CHORD_PUSHCONST(GIScreenProbeTracePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#define DEBUG_HIT_FACE 0

#include "base.hlsli"  
#include "bindless.hlsli"
#include "gltf.h"
#include "cascade_shadow_projection.hlsli"
#include "colorspace.h" 
#include "debug.hlsli" 
#include "debug_line.hlsli"

// Atmosphere shared.
#define ATMOSPHERE_LINEAR_SAMPLER Bindless(SamplerState, pushConsts.linearSampler)
#define DISABLE_ATMOSPHERE_CHECK
#include "atmosphere.hlsli"

[[vk::binding(0, 1)]] RaytracingAccelerationStructure topLevelAS;

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const uint2 gid = remap8x8(localThreadIndex); 
    const uint2 tid = workGroupId * 8 + gid;

    const uint2 probeCoord = workGroupId;
    const uint probeLinearIndex = probeCoord.x + probeCoord.y * pushConsts.probeDim.x;

    GIScreenProbeSpawnInfo spawnInfo;
    { 
        const uint4 packProbeSpawnInfo = BATL(uint4, pushConsts.probeSpawnInfoSRV, probeLinearIndex);
        spawnInfo.unpack(packProbeSpawnInfo);
    }

    // Pre-return if no valid spawn probe. 
    if (!spawnInfo.isValid())
    {
        return;
    }

    // Ray tracing get radiance. 
    float3 probePositionRS;
    float3 probeNormalRS = spawnInfo.normalRS; 
    if (WaveIsFirstLane()) 
    {
        probePositionRS = spawnInfo.getProbePositionRS(pushConsts.gbufferDim, perView); 
    }
    probePositionRS = WaveReadLaneFirst(probePositionRS); 

    // Current cell ray direction, hemisphere based on probe normal. 
    float3 rayDirection = getScreenProbeCellRayDirection(scene, probeCoord, gid, probeNormalRS);

    GIRayQuery query;
    const uint traceFlag = RAY_FLAG_CULL_NON_OPAQUE;

    // 
    float rayStart = spawnInfo.depth > 0.0 ? pushConsts.minRayTraceDistance : 1e-3f;
    RayDesc ray = getRayDesc(probePositionRS, rayDirection, rayStart, pushConsts.maxRayTraceDistance);
    query.TraceRayInline(topLevelAS, traceFlag, 0xFF, ray);
    query.Proceed(); 

    uint singleMieScatteringId = (scene.atmosphere.bCombineScattering == 0) ? pushConsts.singleMieScatteringId : pushConsts.scatteringId;
    Texture2D<float4> transmittanceTexture       = TBindless(Texture2D, float4, pushConsts.transmittanceId);
    Texture2D<float4> irradianceTexture          = TBindless(Texture2D, float4, pushConsts.irradianceTextureId);
    Texture3D<float4> scatteringTexture          = TBindless(Texture3D, float4, pushConsts.scatteringId);
    Texture3D<float4> singleMieScatteringTexture = TBindless(Texture3D, float4, singleMieScatteringId);

    float3 radiance = 0.0;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        // Load hit object info.
        const uint objectId = query.CommittedInstanceID();
        const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
        const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

        const bool bHitFrontFace = query.CommittedTriangleFrontFace();
        const bool bTwoSideMaterial = materialInfo.bTwoSided;
        // Only lighting when ray hit front face of the triangle, when hit back face, we assume it under darkness. 
        [branch] 
        if (bHitFrontFace || bTwoSideMaterial)
        {
            const uint hitPrimitiveIndex = query.CommittedPrimitiveIndex();
            const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
            const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);

            // 
            ByteAddressBuffer lod0IndicesBuffer = ByteAddressBindless(primitiveDataInfo.lod0IndicesBuffer);
            ByteAddressBuffer normalBuffer  = ByteAddressBindless(primitiveDataInfo.normalBuffer);
            ByteAddressBuffer uvDataBuffer  = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);

            float2 uv_tri[3];
            float3 normalRS_tri[3];

            float2 hitTriangleBarycentrics = query.CommittedTriangleBarycentrics();
            float3 bary;
            bary.yz = hitTriangleBarycentrics.xy;
            bary.x  = 1.0 - bary.y - bary.z;

            // TODO: Add ray cone approx miplevel choose. 
            float sampleTextureLod = pushConsts.rayHitLODOffset;

            [unroll(3)]
            for(uint i = 0; i < 3; i ++)
            {
                const uint indicesId = i + hitPrimitiveIndex * 3 + primitiveInfo.lod0IndicesOffset;
                const uint index = primitiveInfo.vertexOffset + lod0IndicesBuffer.TypeLoad(uint, indicesId);

                uv_tri[i] = uvDataBuffer.TypeLoad(float2, index);
                normalRS_tri[i] = normalize(mul(float4(normalBuffer.TypeLoad(float3, index), 0.0), objectInfo.basicData.translatedWorldToLocal).xyz);
            }

            // Get uv and normal.
            float2 uv = uv_tri[0] * bary.x + uv_tri[1] * bary.y + uv_tri[2] * bary.z;
            float3 normalRS = normalRS_tri[0] * bary.x + normalRS_tri[1] * bary.y + normalRS_tri[2] * bary.z;

            // Two side normal sign modify. 
            if (bTwoSideMaterial)
            {
                normalRS *= bHitFrontFace ? 1.0 : -1.0;
            }

            //
            float4 baseColor;
            {
                Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
                SamplerState baseColorSampler = Bindless(SamplerState, materialInfo.baseColorSampler);
                baseColor = baseColorTexture.SampleLevel(baseColorSampler, uv, sampleTextureLod) * materialInfo.baseColorFactor;
            }
            baseColor.xyz = mul(sRGB_2_AP1, baseColor.xyz);

            //
            float3 positionRS = probePositionRS + query.CommittedRayT() * rayDirection;

            float metallic = 0.0;
            const bool bExistAORoughnessMetallicTexture = (materialInfo.metallicRoughnessTexture != kUnvalidIdUint32);
            if (bExistAORoughnessMetallicTexture) 
            {
                Texture2D<float4> metallicRoughnessTexture = TBindless(Texture2D, float4, materialInfo.metallicRoughnessTexture);
                SamplerState metallicRoughnessSampler      = Bindless(SamplerState, materialInfo.metallicRoughnessSampler);

                float4 metallicRoughnessRaw = metallicRoughnessTexture.SampleLevel(metallicRoughnessSampler, uv, sampleTextureLod);
                metallic = metallicRoughnessRaw.g;
            }
            else
            {
                metallic = getFallbackMetallic(materialInfo.metallicFactor);
            }

            // Lighting. 

            float3 diffuseColor = getDiffuseColor(baseColor.xyz, metallic);

            // Apply sun light direct diffuse lighting. 
            {
                float NoL = max(0.0, dot(normalRS, -scene.sunInfo.direction));
                if (NoL > 0.0)
                {
                    CascadeShadowInfo cascadeInfo;
                    cascadeInfo.cacasdeCount           = pushConsts.cascadeCount;
                    cascadeInfo.shadowViewId           = pushConsts.shadowViewId;
                    cascadeInfo.shadowPaddingTexelSize = 0.0;   // Probe don't do any filter so don't need padding texel size.
                    cascadeInfo.positionRS             = positionRS;
                    cascadeInfo.zBias                  = 1e-4f; // This small z bias is fine for most case. 

                    float visibility = 1.0;
                    {
                        uint selectedCascadeId;
                        float3 shadowCoord = fastCascadeSelected(cascadeInfo, perView, selectedCascadeId);
                        if (all(shadowCoord >= 0.0))
                        { 
                            const CascadeShadowDepthIds shadowDepthIds = BATL(CascadeShadowDepthIds, pushConsts.shadowDepthIds, 0);
                            visibility *= cascadeShadowProjection(shadowCoord, selectedCascadeId, perView, shadowDepthIds);
                        }
                        else
                        {
                            // NOTE: Ray trace scene to get ray shadow value without ray sort is slow. 
                            //       We already combine cache cascade shadow map and sdsm, whole scene should cover by cascade shadow map. 
                        }
                    }

                    float3 positionWS_km = float3(double3(positionRS / 1000.0f) + perView.cameraPositionWS_km.getDouble3());
    
                    float3 skyIrradiance;
                    float3 sunIrradiance = GetSunAndSkyIrradiance(
                        scene.atmosphere,     //  
                        transmittanceTexture, //
                        irradianceTexture,    //
                        positionWS_km - scene.atmosphere.earthCenterKm, // Get atmosphere unit position.
                        normalRS,                 // 
                        -scene.sunInfo.direction, //   
                        skyIrradiance);
    
                    // 
                    float3 sunRadiance = finalRadianceExposureModify(scene, skyIrradiance + sunIrradiance);

                    // Do lambert diffuse lighting here.
                    radiance += sunRadiance * visibility * NoL * Fd_LambertDiffuse(diffuseColor);
                } 
            }

            // Infinite indirect lighting. 
            if (pushConsts.bHistoryValid)
            {
                float3 irradiance = 0.0; 
                for (uint cascadeId = 0; cascadeId < pushConsts.clipmapCount; cascadeId ++) 
                {
                    const GIWorldProbeVolumeConfig config = BATL(GIWorldProbeVolumeConfig, pushConsts.clipmapConfigBufferId, cascadeId);
                    if (config.getBlendWeight(positionRS) > 0.99)
                    {
                        SH3_gi s_gi_sh;
                        if (config.sampleSH(perView, positionRS, 1.0 / 100.0, s_gi_sh)) // At least brocast once. 
                        {
                            irradiance = SH_Evalulate(normalRS, s_gi_sh.c) / (2.0 * kPI);
                            break;
                        }
                    } 
                }  
                radiance += irradiance * kInvertPI * diffuseColor; 
            }
        }
 
    #if DEBUG_HIT_FACE
        if(bHitFrontFace)
        {
            radiance = float3(1.0, 0.0, 0.0);
        }
        else
        {
            radiance = float3(0.0, 1.0, 0.0);
        }
    #endif 
    }
    else 
    {
        // Sky sample.
        float3 transmittance;
        radiance = GetSkyRadiance(
            scene.atmosphere,  
            transmittanceTexture, 
            scatteringTexture, 
            singleMieScatteringTexture,  
            perView.cameraToEarthCenter_km.castFloat3(),
            rayDirection, 
            -scene.sunInfo.direction,  
            transmittance); 

        radiance = finalRadianceExposureModify(scene, radiance);
        
    #if DEBUG_HIT_FACE
        radiance = float3(0.0, 0.0, 1.0);
    #endif
    }

    // Store radiance result.
    storeRWTexture2D_float3(pushConsts.radianceUAV, tid, radiance);
}



#endif // 