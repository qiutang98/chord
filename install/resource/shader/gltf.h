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

// Store in GPU scene.
struct GLTFPrimitiveBuffer
{
    chord::GLTFPrimitiveLOD lods[kMaxGLTFLodCount];

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

#ifndef __cplusplus

inline GLTFPrimitiveBuffer fillGLTFPrimitiveBuffer(float4 data[12])
{
    GLTFPrimitiveBuffer result;

    for(int i = 0; i < kMaxGLTFLodCount; i ++)
    {
        result.lods[i].firstIndex   = asuint(data[i].x);
        result.lods[i].indexCount   = asuint(data[i].y);
        result.lods[i].firstMeshlet = asuint(data[i].z);
        result.lods[i].meshletCount = asuint(data[i].w);
    }

    result.posMin = data[kMaxGLTFLodCount + 0].xyz;
    result.primitiveDatasBufferId = asuint(data[kMaxGLTFLodCount + 0].w);

    result.posMax = data[kMaxGLTFLodCount + 1].xyz;
    result.vertexOffset = asuint(data[kMaxGLTFLodCount + 1].w);

    result.posAverage  = data[kMaxGLTFLodCount + 2].xyz;
    result.vertexCount = asuint(data[kMaxGLTFLodCount + 2].w);

    result.color0Offset        = asuint(data[kMaxGLTFLodCount + 3].x);
    result.smoothNormalOffset  = asuint(data[kMaxGLTFLodCount + 3].y); 
    result.textureCoord1Offset = asuint(data[kMaxGLTFLodCount + 3].z);
    result.materialBufferId    = asuint(data[kMaxGLTFLodCount + 3].w);
    
    return result;
}

T_BINDLESS_CONSTATNT_BUFFER_DECLARE(GLTFPrimitiveBuffer)

#endif

// Store in GPUScene.
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

#ifndef __cplusplus

inline GLTFPrimitiveDatasBuffer fillGLTFPrimitiveDatasBuffer(float4 data[3])
{
    GLTFPrimitiveDatasBuffer result;

    result.indicesBuffer       = asuint(data[0].x);
    result.meshletDataBuffer   = asuint(data[0].y);
    result.meshletBuffer       = asuint(data[0].z);
    result.positionBuffer      = asuint(data[0].w);
    result.normalBuffer        = asuint(data[1].x);
    result.textureCoord0Buffer = asuint(data[1].y);
    result.tangentBuffer       = asuint(data[1].z);
    result.textureCoord1Buffer = asuint(data[1].w);
    result.color0Buffer        = asuint(data[2].x);
    result.smoothNormalsBuffer = asuint(data[2].y);

    return result;
}

T_BINDLESS_CONSTATNT_BUFFER_DECLARE(GLTFPrimitiveDatasBuffer)
T_BINDLESS_CONSTATNT_BUFFER_DECLARE(GPUObjectGLTFPrimitive)

#endif