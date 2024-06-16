#include "gltf.h"

#ifndef __cplusplus // HLSL only area.

struct DummyPayLoad
{ 
    uint dummyData; 
}; 
groupshared DummyPayLoad dummyPayLoad;

[numthreads(1, 1, 1)] 
void mainAS()
{
    DispatchMesh(3, 1, 1, dummyPayLoad);
}

struct VertexOutput
{
	float4 position: SV_Position;
	float4 color: COLOR0;
};

static const float4 positions[3] = 
{
	float4( 0.0, -1.0, 0.0, 1.0),
	float4(-1.0,  1.0, 0.0, 1.0),
	float4( 1.0,  1.0, 0.0, 1.0)
};

static const float4 colors[3] = 
{
	float4(0.0, 1.0, 0.0, 1.0),
	float4(0.0, 0.0, 1.0, 1.0),
	float4(1.0, 0.0, 0.0, 1.0)
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void mainMS(
    uint3 DispatchThreadID : SV_DispatchThreadID,
    out indices uint3 triangles[1], 
    out vertices VertexOutput vertices[3])
{
    SetMeshOutputCounts(3, 1);

    float4 offset = float4(0.0, 0.0, (float)DispatchThreadID, 0.0);
    for (uint i = 0; i < 3; i++) 
    {
		vertices[i].position = mul(mvp, positions[i] + offset);
		vertices[i].color = colors[i];
	}

    SetMeshOutputCounts(3, 1);
    triangles[0] = uint3(0, 1, 2);
}

void mainPS(out float4 outColor : SV_Target0)
{

}

#endif