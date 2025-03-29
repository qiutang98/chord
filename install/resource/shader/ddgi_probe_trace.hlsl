#ifndef SHADER_DDGI_PROBE_TRACE_HLSL
#define SHADER_DDGI_PROBE_TRACE_HLSL

#include "ddgi.h"

struct DDGITracePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(DDGITracePushConsts);

    // Current trace random rotation.
    float4x4 randomRotation;

    // 
    uint cameraViewId;
    uint ddgiConfigBufferId;
    uint ddgiConfigId;
    uint probeCacheInfoUAV;

    // 
    uint probeCacheRayGbufferUAV;
    uint probeTraceLinearIndexSRV;
    uint probeTracedMarkSRV;
};
CHORD_PUSHCONST(DDGITracePushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli" 
#include "bindless.hlsli"
#include "gltf.h"
#include "colorspace.h" 
#include "debug.hlsli"
#include "debug_line.hlsli"
#include "raytrace_shared.hlsli"

[[vk::binding(0, 1)]] RaytracingAccelerationStructure topLevelAS;

[numthreads(kDDGITraceThreadCount, 1, 1)]
void mainCS(
    uint2 workGroupId     : SV_GroupID,  
    uint localThreadIndex : SV_GroupIndex,
    uint dispatchId       : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // Load ddgi config volume. 
    const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, pushConsts.ddgiConfigId);

    // Now get ray index. 
    const uint linearProbeIndex = BATL(uint, pushConsts.probeTraceLinearIndexSRV, workGroupId.x);
    const int rayIndex = localThreadIndex + workGroupId.y * kDDGITraceThreadCount;

    // Early return if current ray index out of range. 
    if (rayIndex >= kDDGIPerProbeRayCount)
    {  
        return;  
    } 

    const int3 virtualProbeIndex = ddgiConfig.getVirtualVolumeId(linearProbeIndex);  
    const int  physicsProbeLinearIndex = ddgiConfig.getPhysicalLinearVolumeId(virtualProbeIndex);

    // Safe check.
    if (rayIndex == 0)
    {
        int probeState = BATL(int, pushConsts.probeTracedMarkSRV, physicsProbeLinearIndex);
        check(probeState == 0); // Probe state should set to zero when trace. 
    }

    // Store cache ray index. 
    DDGIProbeCacheTraceInfo probeCacheTraceInfo;

    // Get ray origin. 
    float3 probePositionRS = ddgiConfig.getPositionRS(virtualProbeIndex);
    probePositionRS += BATL(float3, ddgiConfig.offsetSRV, physicsProbeLinearIndex);


    probeCacheTraceInfo.worldPosition.fill(perView.cameraWorldPos.getDouble3() + double3(probePositionRS)); // Cache world space position of the probe for relighting convenient.

    // Get ray rotation.
    probeCacheTraceInfo.rayRotation = pushConsts.randomRotation;
    if (rayIndex == 0)
    { 
        // Update probe cache trace info. 
        BATS(DDGIProbeCacheTraceInfo, pushConsts.probeCacheInfoUAV, physicsProbeLinearIndex, probeCacheTraceInfo);
    }
 
    float3 rayDirection = ddgiConfig.getSampleRayDir(rayIndex);
    rayDirection = mul(probeCacheTraceInfo.rayRotation, float4(rayDirection, 0.0)).xyz;

    //
    GIRayQuery query;
    const uint traceFlag = RAY_FLAG_CULL_NON_OPAQUE;
    RayDesc ray = getRayDesc(probePositionRS, rayDirection, 0.1, ddgiConfig.rayTraceMaxDistance);
    query.TraceRayInline(topLevelAS, traceFlag, 0xFF, ray);
    query.Proceed();

    // Generate mini gbuffer.
    DDGIProbeCacheMiniGbuffer miniGbuffer = (DDGIProbeCacheMiniGbuffer)0;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        const bool bHitTriangleBackFace = !query.CommittedTriangleFrontFace();
        // 
        miniGbuffer.setRayHitDistance(query.CommittedRayT(), bHitTriangleBackFace);
        [branch]
        if (!bHitTriangleBackFace)
        {
            //
            const uint hitPrimitiveIndex = query.CommittedPrimitiveIndex();

            // Load hit object info.
            const uint objectId = query.CommittedInstanceID();
            const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
            const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
            const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);
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

            //
            float4 baseColor;
            {
                Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
                SamplerState baseColorSampler = Bindless(SamplerState, materialInfo.baseColorSampler);
                baseColor = baseColorTexture.SampleLevel(baseColorSampler, uv, ddgiConfig.rayHitSampleTextureLod) * materialInfo.baseColorFactor;
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

                float4 metallicRoughnessRaw = metallicRoughnessTexture.SampleLevel(metallicRoughnessSampler, uv, ddgiConfig.rayHitSampleTextureLod);
                metallic = metallicRoughnessRaw.g;
            }
            else
            {
                metallic = getFallbackMetallic(materialInfo.metallicFactor);
            }

            // Load mini gbuffer info.
            miniGbuffer.normalRS  = normalRS;
            miniGbuffer.baseColor = baseColor.xyz; 
            miniGbuffer.metallic  = metallic;
        }
    }
    else
    {
        // Small epsilon make > compare work.
        miniGbuffer.distance = ddgiConfig.rayTraceMaxDistance + 1e-3f;
    }

    // Store mini gbuffer.
    int storePosition = physicsProbeLinearIndex * kDDGIPerProbeRayCount + rayIndex;
    BATS(DDGIProbeCacheMiniGbuffer, pushConsts.probeCacheRayGbufferUAV, storePosition, miniGbuffer);

}

#endif //!__cplusplus

#endif // SHADER_DDGI_PROBE_TRACE_HLSL