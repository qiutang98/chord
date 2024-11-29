#ifndef SHADER_GLTF_H
#define SHADER_GLTF_H

#include "base.h"

#ifndef __cplusplus
#include "bindless.hlsli"
#endif

#define kFrustumCullingEnableBit  0
#define kHZBCullingEnableBit      1
#define kMeshletConeCullEnableBit 2

// id = ~0 if no exist.

struct GPUBVHNode
{
    float4 sphere;
    uint children[kNaniteBVHLevelNodeCount];

    uint bvhNodeCount;
    uint leafMeshletGroupOffset;
    uint leafMeshletGroupCount; 
};

struct GPUGLTFMeshletGroup
{
    float3 clusterPosCenter;
    float parentError;

    float3 parentPosCenter; 
    float error;

    uint meshletOffset;
    uint meshletCount;
};

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
};

inline float getFallbackMetallic(float metallicFactor)
{
    // I don't know why GLTF default set material metallicFactor as 1.0.
    // Most material fallback should set to zero if no set.
    return metallicFactor >= 1.0 ? 0.0f : metallicFactor;
}

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
    uint color0Offset; 
    uint smoothNormalOffset;
    uint textureCoord1Offset;

    uint bvhNodeOffset;
    uint meshletGroupOffset;
    uint meshletGroupIndicesOffset;
    uint meshletGroupCount;

    uint lod0IndicesOffset;
    uint lod0IndicesCount;
    uint pad0;
    uint pad1;
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
    uint bvhNodeBuffer;
    uint meshletGroupBuffer;
    uint meshletGroupIndicesBuffer;

    uint lod0IndicesBuffer;
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

    float3 emissiveFactor;
    uint emissiveTexture;

    // GLTF Specification for metallicRoughnessTexture
    // 

    // The textures for metalness and roughness properties are packed together in a single texture called metallicRoughnessTexture. 
    // Its green channel contains roughness values and its blue channel contains metalness values. 
    // This texture MUST be encoded with linear transfer function and MAY use more than 8 bits per channel.

    // NOTE: I encode linear occlusion in red channel if exist. check by bExistOcclusion value.
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
    uint materialType;
};
CHORD_CHECK_SIZE_GPU_SAFE(GLTFMaterialGPUData);

#endif // !SHADER_GLTF_H