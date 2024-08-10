#ifndef SHADER_GLTF_H
#define SHADER_GLTF_H

#include "base.h"

#ifndef __cplusplus
#include "bindless.hlsli"
#endif

// id = ~0 if no exist.

#define kMaxGLTFLodCount       8

struct GPUGLTFPrimitiveLOD
{
    uint firstIndex;
    uint indexCount;
    uint firstMeshlet;
    uint meshletCount;
};

struct GPUGLTFMeshlet
{
    float3 posMin;
    uint   firstIndex;

    float3 posMax;
    uint   triangleCount;

    float3 coneAxis;
    float  coneCutOff;

    float3 coneApex;
    float  pad0;
};

// Store in GPU scene.
struct GLTFPrimitiveBuffer
{
    GPUGLTFPrimitiveLOD lods[kMaxGLTFLodCount];

    float3 posMin;
    uint primitiveDatasBufferId;

    float3 posMax;
    uint vertexOffset;

    float3 posAverage;
    uint vertexCount;

    uint color0Offset; 
    uint smoothNormalOffset; 
    uint textureCoord1Offset;
    uint lodCount;

    float lodBase;
    float loadStep;
    float lodScreenPercentageScale;
    uint pad3;
};
CHORD_CHECK_SIZE_GPU_SAFE(GLTFPrimitiveBuffer);

// Store in GPUScene.
struct GLTFPrimitiveDatasBuffer
{
    uint indicesBuffer;
    uint meshletBuffer;
    uint positionBuffer;
    uint normalBuffer;

    uint textureCoord0Buffer;
    uint tangentBuffer;
    uint textureCoord1Buffer;
    uint color0Buffer;

    uint smoothNormalsBuffer;
    uint pad0;
    uint pad1;
    uint pad2;
};
CHORD_CHECK_SIZE_GPU_SAFE(GLTFPrimitiveDatasBuffer);

struct GLTFMaterialGPUData
{
    uint alphaMode;
    float alphaCutOff;
    uint bTwoSided;
    uint baseColorId;

    float4 baseColorFactor;

    uint emissiveTexture;
    float3 emissiveFactor;

    float metallicFactor;
    float roughnessFactor;
    uint metallicRoughnessTexture;
    uint normalTexture;

    uint baseColorSampler;
    uint emissiveSampler;
    uint normalSampler;
    uint metallicRoughnessSampler;
    
    float normalFactorScale;
    uint bExistOcclusion;
    float occlusionTextureStrength;
    float pad0;
};
CHORD_CHECK_SIZE_GPU_SAFE(GLTFMaterialGPUData);

#endif // !SHADER_GLTF_H