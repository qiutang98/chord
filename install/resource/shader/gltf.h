#pragma once 

#include "base.h"

// id = ~0 if no exist.

#define  kMaxGLTFLodCount 8

namespace chord
{

struct GLTFPrimitiveLOD
{
    ARCHIVE_DECLARE;

    uint firstIndex;
    uint indexCount;
    uint firstMeshlet;
    uint meshletCount;
};
CHORD_CHECK_SIZE_GPU_SAFE(GLTFPrimitiveLOD);

struct GLTFMeshlet
{
    ARCHIVE_DECLARE;

    float3 posMin;
    uint vertexCount;

    float3 posMax;
    uint triangleCount;

    float3 posAverage;
    uint dataOffset;
};
CHORD_CHECK_SIZE_GPU_SAFE(GLTFMeshlet);
}

struct GLTFPrimitiveBuffer
{
    GLTFPrimitiveLOD lods[kMaxGLTFLodCount];

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

struct GLTFPrimitiveDatasBuffer
{
    uint indicesBuffer;
    uint meshletDataBuffer;
    uint meshletBuffer;
    uint positionBuffer;

    uint normalBuffer;
    uint textureCoord0Buffer;
    uint tangentBuffer;
    uint textureCoord1Buffer;

    uint color0Buffer;
    uint smoothNormalsBuffer;
};