// Debug draw line index.
// 

#include "base.h"

struct DebugLinePushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(DebugLinePushConst);
    uint cameraViewId;
    uint debugLineCPUVertices;
    uint gpuDrawCmdId;
};
CHORD_PUSHCONST(DebugLinePushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"

[numthreads(1, 1, 1)]
void gpuFillIndirectCS()
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;  

    uint4 cmd;
    cmd.x = BATL(uint, scene.debuglineCount, 0); // vertexCount
    cmd.y = 1; // instanceCount
    cmd.z = 0; // firstVertex
    cmd.w = 0; // firstInstance

    BATS(uint4, pushConsts.gpuDrawCmdId, 0, cmd);
}

struct VS2PS
{
    float4 positionHS : SV_Position;
    float4 color : COLOR0;
};

void mainVS(uint vertexId : SV_VertexID, out VS2PS output)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);

    LineDrawVertex vertex;
#if DIM_GPU
    vertex = BATL(LineDrawVertex, perView.basicData.debuglineVertices, vertexId);
#else 
    vertex = BATL(LineDrawVertex, pushConsts.debugLineCPUVertices, vertexId);
#endif

    output.positionHS = mul(perView.translatedWorldToClip, float4(vertex.translatedWorldPos, 1.0));
    output.color = shaderUnpackColor(vertex.color);
}

void mainPS(in VS2PS input, out float4 outSceneColor : SV_Target0)
{
    outSceneColor = input.color;
}

#endif // !__cplusplus