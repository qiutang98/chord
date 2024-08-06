#ifndef DEBUG_LINE_HLSLI
#define DEBUG_LINE_HLSLI

#include "base.hlsli"
#include "bindless.hlsli"

uint allocateDebugDrawLineId(in const GPUBasicData scene, uint vertexCount)
{
    uint drawId;
    RWByteAddressBuffer drawedCountBuffer = RWByteAddressBindless(scene.debuglineCount);
    drawedCountBuffer.InterlockedAdd(0, vertexCount, drawId);

    if (drawId >= scene.debuglineMaxCount)
    {
        uint printId;
        drawedCountBuffer.InterlockedAdd(1, 1, printId);
        if (printId == 10) // Print 10 times message.
        {
            printf("Debug line vertex count already overflow, current max count is '%d'.", scene.debuglineMaxCount);
        }
        return ~0;
    }

    return drawId;
}

inline void addLine(in const GPUBasicData scene, LineDrawVertex startEnd[2])
{
    uint drawId = allocateDebugDrawLineId(scene, 2);
    if (drawId == ~0) { return; }

    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, startEnd[0]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, startEnd[1]);
}

// WorldPosExtent apply with center + extent * kExtentApplyFactor[i] pattern.
inline void addBox(in const GPUBasicData scene, LineDrawVertex worldPosExtent[8])
{
    uint drawId = allocateDebugDrawLineId(scene, 24);
    if (drawId == ~0) { return; }

    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[0]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[2]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[0]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[3]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[0]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[4]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[2]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[5]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[2]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[7]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[1]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[5]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[1]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[6]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[1]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[7]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[4]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[7]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[4]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[6]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[3]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[6]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[3]);
    BATS(LineDrawVertex, scene.debuglineVertices, drawId ++, worldPosExtent[5]);
}

#endif