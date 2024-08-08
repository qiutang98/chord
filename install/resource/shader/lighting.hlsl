#include "gltf.h"



struct LightingPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(LightingPushConsts);

    float2 visibilityTexelSize;
    uint2 visibilityDim;
    uint cameraViewId;
    uint tileBufferCmdId;
    uint visibilityId;
    uint sceneColorId;
};
CHORD_PUSHCONST(LightingPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"

float3 gltfMetallicRoughnessPBR(uint objectId, uint indicesId, float2 uv)
{
    return float3(uv.xy, 0.0);
}

float3 noneShading(uint objectId, uint indicesId, float2 uv)
{
    return 1;
}

void storeColor(uint2 pos, float3 c)
{
    RWTexture2D<float3> rwSceneColor = TBindless(RWTexture2D, float3, pushConsts.sceneColorId);
    rwSceneColor[pos] = c;
}

[numthreads(64, 1, 1)]
void mainCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    uint2 tileOffset = BATL(uint2, pushConsts.tileBufferCmdId, workGroupId);
    uint2 dispatchPos = tileOffset + remap8x8(localThreadIndex);

    // Edge return.
    if (any(dispatchPos >= pushConsts.visibilityDim))
    {
        return;
    }

    Texture2D<uint2> visibilityTexture = TBindless(Texture2D, uint2, pushConsts.visibilityId);
    const uint2 packIDs = visibilityTexture[dispatchPos];

    const uint indicesId = packIDs.y;
    uint objectId;
    uint shadingType;
    decodeObjectInfo(packIDs.x, shadingType, objectId);

    const float2 uv = (dispatchPos + 0.5) * pushConsts.visibilityTexelSize;

    #if LIGHTING_TYPE == kLightingType_None
    {
        if (shadingType != kLightingType_None) { return; }
        storeColor(dispatchPos, noneShading(objectId, indicesId, uv));
    }
    #elif LIGHTING_TYPE == kLightingType_GLTF_MetallicRoughnessPBR
    {
        if (shadingType != kLightingType_GLTF_MetallicRoughnessPBR) { return; }
        storeColor(dispatchPos, gltfMetallicRoughnessPBR(objectId, indicesId, uv));
    }
    #endif 
}


#endif // !__cplusplus