// Visualize pass for accelerate structure. 

#include "base.h"

struct AccelerationStructureVisualizePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(AccelerationStructureVisualizePushConsts);

    uint cameraViewId;
    uint uav;

    uint cascadeCount;
    uint shadowViewId;
    uint shadowDepthIds;

    uint transmittanceId;
    uint scatteringId;
    uint singleMieScatteringId;
    uint linearSampler;
};
CHORD_PUSHCONST(AccelerationStructureVisualizePushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli"
#include "bindless.hlsli"
#include "gltf.h"
#include "cascade_shadow_projection.hlsli"
#include "colorspace.h"

// Atmosphere shared.
#define ATMOSPHERE_LINEAR_SAMPLER Bindless(SamplerState, pushConsts.linearSampler)
#define DISABLE_ATMOSPHERE_CHECK
#include "atmosphere.hlsli"

[[vk::binding(0, 1)]] RaytracingAccelerationStructure topLevelAS;

[numthreads(8, 8, 1)]
void mainCS(uint2 workPos : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint2 renderDim = uint2(perView.renderDimension.xy);
    if (any(workPos >= renderDim))
    {
        return;
    }

    const float2 fragCoord = workPos + 0.5;
    const float2 uv = fragCoord * perView.renderDimension.zw;

    float3 worldDirectionRS;
    {
        const float4 viewPointCS = float4(screenUvToNdcUv(uv), kFarPlaneZ, 1.0);
        float4 viewPointRS = mul(perView.clipToTranslatedWorld, viewPointCS);
        worldDirectionRS = normalize(viewPointRS.xyz / viewPointRS.w);
    }

    float3 resultColor = 0.0;

    GIRayQuery query;
    RayDesc ray = getRayDesc(0, worldDirectionRS, 0.0, kDefaultRayQueryTMax);

    // 
    const uint traceFlag = RAY_FLAG_CULL_NON_OPAQUE;

    query.TraceRayInline(topLevelAS, traceFlag, 0xFF, ray);
    query.Proceed();
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
    #if 0
        query.CommittedInstanceIndex();
        query.CommittedGeometryIndex();
        float t = query.CommittedRayT();
        query.CommittedTriangleFrontFace();
    #endif

        const uint hitPrimitiveIndex = query.CommittedPrimitiveIndex();

        const uint objectId = query.CommittedInstanceID();
        const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
        const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
        const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);
        const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
        
        // 
        ByteAddressBuffer lod0IndicesBuffer = ByteAddressBindless(primitiveDataInfo.lod0IndicesBuffer);

        // 
        ByteAddressBuffer normalBuffer  = ByteAddressBindless(primitiveDataInfo.normalBuffer);
        ByteAddressBuffer uvDataBuffer  = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);
        ByteAddressBuffer tangentBuffer = ByteAddressBindless(primitiveDataInfo.tangentBuffer);

        float2 uv_tri[3];
        float3 normalRS_tri[3];

        float2 hitTriangleBarycentrics = query.CommittedTriangleBarycentrics();
        float3 bary;
        bary.yz = hitTriangleBarycentrics.xy;
        bary.x  = 1.0 - bary.y - bary.z;

        [unroll(3)]
        for(uint i = 0; i < 3; i ++)
        {
            const uint indicesId = i + hitPrimitiveIndex * 3 + primitiveInfo.lod0IndicesOffset;
            const uint index = primitiveInfo.vertexOffset + lod0IndicesBuffer.TypeLoad(uint, indicesId);

            uv_tri[i] = uvDataBuffer.TypeLoad(float2, index);

            // Normal convert to relative camera space.
            normalRS_tri[i] = normalize(mul(float4(normalBuffer.TypeLoad(float3, index), 0.0), objectInfo.basicData.translatedWorldToLocal).xyz);
        }


        float2 uv = uv_tri[0] * bary.x + uv_tri[1] * bary.y + uv_tri[2] * bary.z;
        float3 normalRS = normalRS_tri[0] * bary.x + normalRS_tri[1] * bary.y + normalRS_tri[2] * bary.z;

        float4 baseColor;
        {
            Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
            SamplerState baseColorSampler      = Bindless(SamplerState, materialInfo.baseColorSampler);
            baseColor = baseColorTexture.SampleLevel(baseColorSampler, uv, 0) * materialInfo.baseColorFactor;
        }
        baseColor.xyz = mul(sRGB_2_AP1, baseColor.xyz);

        CascadeShadowInfo cascadeInfo;
        cascadeInfo.cacasdeCount           = pushConsts.cascadeCount;
        cascadeInfo.shadowViewId           = pushConsts.shadowViewId;
        cascadeInfo.shadowPaddingTexelSize = 0.0; // Current don't do any filter.
        cascadeInfo.positionRS             = query.CommittedRayT() * worldDirectionRS;
        cascadeInfo.zBias                  = 1e-4f; // This small z bias is fine for most case. 

        float shadowValue = 1.0;
        {
            uint selectedCascadeId;
            float3 shadowCoord = fastCascadeSelected(cascadeInfo, perView, selectedCascadeId);
            if (all(shadowCoord >= 0.0))
            {
                const CascadeShadowDepthIds shadowDepthIds = BATL(CascadeShadowDepthIds, pushConsts.shadowDepthIds, 0);
                shadowValue = cascadeShadowProjection(shadowCoord, selectedCascadeId, perView, shadowDepthIds);
            }
        } 

        resultColor.xyz = shadowValue * max(0.0, dot(normalRS, -scene.sunInfo.direction)) * baseColor.xyz;
    }
    else
    {

        uint singleMieScatteringId = (scene.atmosphere.bCombineScattering == 0) ? pushConsts.singleMieScatteringId : pushConsts.scatteringId;
        Texture2D<float4> transmittanceTexture       = TBindless(Texture2D, float4, pushConsts.transmittanceId);
        Texture3D<float4> scatteringTexture          = TBindless(Texture3D, float4, pushConsts.scatteringId);
        Texture3D<float4> singleMieScatteringTexture = TBindless(Texture3D, float4, singleMieScatteringId);

        float3 transmittance;
        float3 radiance = GetSkyRadiance(
            scene.atmosphere,  
            transmittanceTexture,
            scatteringTexture, 
            singleMieScatteringTexture,  
            perView.cameraToEarthCenter_km.castFloat3(),
            worldDirectionRS, 
            -scene.sunInfo.direction,  
            transmittance); 

        resultColor.xyz = finalRadianceExposureModify(scene, radiance);
    }

    storeRWTexture2D_float3(pushConsts.uav, workPos, resultColor);
}

#endif 