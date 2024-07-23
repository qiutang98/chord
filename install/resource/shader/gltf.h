#pragma once 

#include "base.h"

#ifndef __cplusplus
#include "bindless.hlsli"
#endif

// id = ~0 if no exist.

#define kMaxGLTFLodCount       8
#define kMeshletMaxVertices   64
#define kMeshletMaxTriangles 124

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
    uint firstIndex;

    float3 posMax;
    uint triangleCount;

    float3 posAverage;
    uint pad0;
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
    uint materialBufferId;
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