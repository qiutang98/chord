#include "gltf.h"

struct GLTFDrawPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(GLTFDrawPushConsts);

    uint cameraViewId;
    uint debugFlags;

    uint cullCountBuffer;
    uint cullObjectIdBuffer; 
};
CHORD_PUSHCONST(GLTFDrawPushConsts, pushConsts);


#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "debug.hlsli"

struct VertexOutput
{
	float4 position: SV_Position;
	float4 color: COLOR0;
}; 

struct Payload
{ 
    uint meshletIndices[GPU_WAVE_THREADS];
}; 

// Per-object level culling pass.
[numthreads(64, 1, 1)]
void perObjectCullCS(
    uint dispatchThreadID : SV_DispatchThreadID, 
    uint groupThreadID : SV_GroupThreadID, 
    uint groupID : SV_GroupID)
{
    // Get view data.
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);

    // Pre-return if reach edge.
    const uint gltfObjectCount = perView.basicData.GLTFObjectCount;
    if (dispatchThreadID >= gltfObjectCount)
    {
        return;
    }

    StructuredBuffer<GPUObjectGLTFPrimitive> objectDataBuffer = TBindless(StructuredBuffer, GPUObjectGLTFPrimitive, perView.basicData.GLTFObjectBuffer);
    const GPUObjectGLTFPrimitive objectInfo = objectDataBuffer[dispatchThreadID];

    const uint GLTFPrimitiveDetailBufferId = perView.basicData.GLTFPrimitiveDetailBuffer;
    StructuredBuffer<GLTFPrimitiveBuffer> primitiveBuffer = TBindless(StructuredBuffer, GLTFPrimitiveBuffer, GLTFPrimitiveDetailBufferId);
    const GLTFPrimitiveBuffer primitiveInfo = primitiveBuffer[objectInfo.GLTFPrimitiveDetail];

    bool bVisible = true;
    {
        // Visible culling. 
    }

    if (!bVisible)
    {
        return;
    }

    uint selectedLOD = 0;
    {
        // LOD selecting. 
    }

    const chord::GLTFPrimitiveLOD lodInfo = primitiveInfo.lods[selectedLOD];


    RWStructuredBuffer<uint> rwCountBuffer = TBindless(RWStructuredBuffer, uint, pushConsts.cullCountBuffer);
    RWStructuredBuffer<uint2> rwIdBuffer = TBindless(RWStructuredBuffer, uint2, pushConsts.cullCountBuffer);

    uint storeIndex;
    InterlockedAdd(rwCountBuffer[0], 1, storeIndex);

    // Store object index.
    rwIdBuffer[storeIndex] = dispatchThreadID;
}

// Amplify shader export data. 
groupshared Payload amplifyPayload; 

groupshared uint sharedInstanceCount[kMaxGLTFLodCount];

// Meshlet level culling.
[numthreads(GPU_WAVE_THREADS, 1, 1)] 
void mainAS(
    uint dispatchThreadID : SV_DispatchThreadID, 
    uint groupThreadID : SV_GroupThreadID, 
    uint groupID : SV_GroupID)
{ 
    // Get view data.
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);

    bool bVisible = true;

    if (bVisible)
    {
        uint index = WavePrefixCountBits(bVisible);
        amplifyPayload.meshletIndices[index] = dispatchThreadID;
    }

    uint visibleCount = WaveActiveCountBits(bVisible);
    DispatchMesh(visibleCount, 1, 1, amplifyPayload);
}


// Triangle assembly.
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void mainMS(
    uint3 DispatchThreadID : SV_DispatchThreadID,
    in payload DummyPayLoad dummyPayLoad,
    out indices uint3 triangles[1], 
    out vertices VertexOutput vertices[3])
{
    SetMeshOutputCounts(3, 1);

    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    float4x4 translatedWorldToClip = dummyPayLoad.TestLoad;

    vertices[0].position = mul(translatedWorldToClip, positions[0]);
    vertices[1].position = mul(translatedWorldToClip, positions[1]);
    vertices[2].position = mul(translatedWorldToClip, positions[2]);

    vertices[0].color = colors[0];
    vertices[1].color = colors[1];
    vertices[2].color = colors[2];

    SetMeshOutputCounts(3, 1);
    triangles[0] = uint3(0, 2, 1);


}

// Draw pixel color.
void mainPS(
    in VertexOutput input,
    out float4 outColor : SV_Target0)
{
    outColor = input.color;
}

#endif