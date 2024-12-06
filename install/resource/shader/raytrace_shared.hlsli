#ifndef SHADER_RAY_TRACE_SHARED_HLSLI
#define SHADER_RAY_TRACE_SHARED_HLSLI

#include "base.hlsli" 
#include "bindless.hlsli"
#include "gltf.h"
#include "colorspace.h" 
#include "debug.hlsli"

float getRayTraceStartOffset(float zNear, float deviceZ)
{
    // https://www.desmos.com/calculator/fbvocp9yvd
    const float linearZ = zNear / deviceZ;
    const float x = linearZ * 0.01;

    return lerp(5e-3f, 1e-1f, x / (x + 1.0));
}

struct RayHitMaterialInfo
{
    float4 baseColor;
    float3 emissiveColor;
    float  hitT;
    float3 diffuseColor;
    float  metallic;
    float3 normalRS;
    uint   objectId;

    float  roughness;
    bool   bHitFrontFace;
    bool   bTwoSideMaterial;


    bool isValidHit()
    {
        return bHitFrontFace || bTwoSideMaterial;
    }

    void init(in const GPUBasicData scene, in const GIRayQuery query, float rayHitTextureLODOffset = 0.0)
    {
        check(query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);

    #if 0
        query.CommittedInstanceIndex();
        query.CommittedGeometryIndex();
    #endif

        //
        hitT = query.CommittedRayT();

        // 
        objectId = query.CommittedInstanceID();
        bHitFrontFace = query.CommittedTriangleFrontFace();

        const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
        const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

        //
        bTwoSideMaterial = materialInfo.bTwoSided;

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

        float sampleTextureLod = rayHitTextureLODOffset; // 

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
        normalRS = normalize(normalRS_tri[0] * bary.x + normalRS_tri[1] * bary.y + normalRS_tri[2] * bary.z);

        // Two side normal sign modify. 
        if (bTwoSideMaterial)
        {
            normalRS *= bHitFrontFace ? 1.0 : -1.0;
        }

        //
        {
            Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
            SamplerState baseColorSampler = Bindless(SamplerState, materialInfo.baseColorSampler);
            baseColor = baseColorTexture.SampleLevel(baseColorSampler, uv, sampleTextureLod) * materialInfo.baseColorFactor;
        }
        baseColor.xyz = mul(sRGB_2_AP1, baseColor.xyz);

        {
            Texture2D<float4> emissiveTexture = TBindless(Texture2D, float4, materialInfo.emissiveTexture);
            SamplerState emissiveSampler = Bindless(SamplerState, materialInfo.emissiveSampler);
            emissiveColor = emissiveTexture.SampleLevel(emissiveSampler, uv, sampleTextureLod).xyz * materialInfo.emissiveFactor;
        }
        emissiveColor = mul(sRGB_2_AP1, emissiveColor);
        //

        const bool bExistAORoughnessMetallicTexture = (materialInfo.metallicRoughnessTexture != kUnvalidIdUint32);
        if (bExistAORoughnessMetallicTexture) 
        {
            Texture2D<float4> metallicRoughnessTexture = TBindless(Texture2D, float4, materialInfo.metallicRoughnessTexture);
            SamplerState metallicRoughnessSampler      = Bindless(SamplerState, materialInfo.metallicRoughnessSampler);

            float4 metallicRoughnessRaw = metallicRoughnessTexture.SampleLevel(metallicRoughnessSampler, uv, sampleTextureLod);
            metallic  = metallicRoughnessRaw.g;
            roughness = metallicRoughnessRaw.b;
        }
        else
        {
            roughness = materialInfo.roughnessFactor;
            metallic = getFallbackMetallic(materialInfo.metallicFactor);
        }

        // 
        diffuseColor = getDiffuseColor(baseColor.xyz, metallic);
    }
};

#endif