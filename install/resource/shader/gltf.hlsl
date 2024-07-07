#include "gltf.h"

struct GLTFDrawPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(GLTFDrawPushConsts);

    uint cameraViewId;
    uint debugFlags;
};
CHORD_PUSHCONST(GLTFDrawPushConsts, pushConsts);


#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "debug.hlsli"

struct DummyPayLoad
{ 
    float4x4 TestLoad;
}; 
groupshared DummyPayLoad dummyPayLoad;

[numthreads(1, 1, 1)] 
void mainAS()
{ 
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    dummyPayLoad.TestLoad = perView.translatedWorldToClip;

#if DEBUG_PRINT
    debug::printFormat(dummyPayLoad.TestLoad);
#endif

    DispatchMesh(3, 1, 1, dummyPayLoad);
}

struct VertexOutput
{
	float4 position: SV_Position;
	float4 color: COLOR0;
}; 

#if MESH_SHADER

#if POSITION_MODE == 5
static const float4 positions[3] = 
{
	float4(100, 0,  0, 1.0),
	float4(  0, 0,  0, 1.0),
	float4( 50, 0, 50, 1.0)
};
#elif POSITION_MODE == 9
static const float4 positions[3] = 
{
	float4(10, 0, 0, 1.0),
	float4( 0, 0, 0, 1.0),
	float4( 5, 0, 5, 1.0)
};
#else 
#error "error entry."
#endif

#if COLOR_MODE == 0
static const float4 colors[3] = 
{
	float4(1, 1, 0, 1),
	float4(0, 1, 1, 1),
	float4(0, 1, 1, 1)
};
#elif COLOR_MODE == 1
static const float4 colors[3] = 
{
	float4(1, 1, 0, 1),
	float4(0, 1, 1, 1),
	float4(1, 0, 1, 1)
};
#elif COLOR_MODE == 2
static const float4 colors[3] = 
{
	float4(1, 0.5, 0.25, 1),
	float4(0.25, 1, 0.5, 1),
	float4(0.5, 0.25, 1, 1)
};
#else 
#error "error entry."
#endif

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
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

#endif 

void mainPS(
    in VertexOutput input,
    out float4 outColor : SV_Target0)
{
    outColor = input.color;
}

#endif