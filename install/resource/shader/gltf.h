#ifndef SHADER_GLTF_H
#define SHADER_GLTF_H

#include "base.h"

#ifndef __cplusplus
#include "bindless.hlsli"
#endif

// id = ~0 if no exist.

struct GPUGLTFMeshlet
{
    float3 posMin;
    uint dataOffset;

    float3 posMax;
    uint   vertexTriangleCount; // 8 bit: vertex count, 8 bit triangle count.

    float3 coneAxis;
    float  coneCutOff;

    float3 coneApex;
    uint lod;

    float3 clusterPosCenter;
    float parentError;

    float3 parentPosCenter; 
    float error;
};

inline uint packVertexCountTriangleCount(uint vertexCount, uint triangleCount) { return (vertexCount & 0xff) | ((triangleCount & 0xff) << 8); }
inline uint unpackVertexCount(uint pack) { return pack & 0xff; }
inline uint unpackTriangleCount(uint pack) { return (pack >> 8) & 0xff; }

// Store in GPU scene.
struct GLTFPrimitiveBuffer
{
    float3 posMin;
    uint primitiveDatasBufferId;

    float3 posMax;
    uint vertexOffset;

    float3 posAverage;
    uint vertexCount;


    uint meshletOffset;
    uint meshletCount;
    uint color0Offset; 
    uint smoothNormalOffset;

    uint textureCoord1Offset;
    uint pad0;
    uint pad1;
    uint pad2;
};
CHORD_CHECK_SIZE_GPU_SAFE(GLTFPrimitiveBuffer);

// Store in GPUScene.
struct GLTFPrimitiveDatasBuffer
{
    uint meshletBuffer;
    uint positionBuffer;
    uint normalBuffer;
    uint textureCoord0Buffer;

    uint tangentBuffer;
    uint textureCoord1Buffer;
    uint color0Buffer;
    uint smoothNormalsBuffer;

    uint meshletDataBuffer;
    uint pad1;
    uint pad2;
    uint pad3;
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