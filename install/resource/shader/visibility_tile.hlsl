#include "gltf.h"

struct VisibilityTilePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(VisibilityTilePushConsts);
    
    float2 visibilityTexelSize;
    uint2 markerDim;
    uint visibilityId;
    uint markerTextureId;

    uint markerIndex;
    uint markerBit;
    uint tileBufferCountId;
    uint tileBufferCmdId;
    uint tileDrawCmdId;

    uint gatherSampler;
};
CHORD_PUSHCONST(VisibilityTilePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"

groupshared uint sTileMarkerR[8][8];
groupshared uint sTileMarkerG[8][8];
groupshared uint sTileMarkerB[8][8];
groupshared uint sTileMarkerA[8][8];

[numthreads(64, 1, 1)]
void tilerMarkerCS(uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    Texture2D<uint2> visibilityTexture = TBindless(Texture2D, uint2, pushConsts.visibilityId);
    RWTexture2D<uint4> rwTileMarker = TBindless(RWTexture2D, uint4, pushConsts.markerTextureId);
    SamplerState gatherSampler = Bindless(SamplerState, pushConsts.gatherSampler);

    uint2 remapId = remap8x8(localThreadIndex);

    // Per-bit store one shading type usage state. 
    // Total used 128 bit, uint4.
    uint4 shadingTileMarker = 0;

    [unroll(2)]
    for (uint y = 0; y < 2; y ++)
    {
        [unroll(2)]
        for (uint x = 0; x < 2; x ++)
        {
            uint2 gatherPos = workGroupId * 32 + 4 * remapId + 2 * uint2(x, y);
            float2 gatherUv = (gatherPos + 1.0) * pushConsts.visibilityTexelSize;
            uint4 shadingType4 = getShadingType4(visibilityTexture.Gather(gatherSampler, gatherUv));

            [unroll(4)]
            for (uint i = 0; i < 4; i ++)
            {
                uint shadingType = shadingType4[i];
                shadingTileMarker[shadingType / 32] |= 1U << (shadingType % 32);
            }
        }
    }

    sTileMarkerR[remapId.x][remapId.y] = shadingTileMarker.r;
    sTileMarkerG[remapId.x][remapId.y] = shadingTileMarker.g;
    sTileMarkerB[remapId.x][remapId.y] = shadingTileMarker.b;
    sTileMarkerA[remapId.x][remapId.y] = shadingTileMarker.a;

    GroupMemoryBarrierWithGroupSync();
    if (remapId.x % 2 == 0)
    {
        sTileMarkerR[remapId.x][remapId.y] |= sTileMarkerR[remapId.x + 1][remapId.y];
        sTileMarkerG[remapId.x][remapId.y] |= sTileMarkerG[remapId.x + 1][remapId.y];
        sTileMarkerB[remapId.x][remapId.y] |= sTileMarkerB[remapId.x + 1][remapId.y];
        sTileMarkerA[remapId.x][remapId.y] |= sTileMarkerA[remapId.x + 1][remapId.y];
    }
    GroupMemoryBarrierWithGroupSync();
    if (remapId.y % 2 == 0)
    {
        sTileMarkerR[remapId.x][remapId.y] |= sTileMarkerR[remapId.x][remapId.y + 1];
        sTileMarkerG[remapId.x][remapId.y] |= sTileMarkerG[remapId.x][remapId.y + 1];
        sTileMarkerB[remapId.x][remapId.y] |= sTileMarkerB[remapId.x][remapId.y + 1];
        sTileMarkerA[remapId.x][remapId.y] |= sTileMarkerA[remapId.x][remapId.y + 1];
    }
    GroupMemoryBarrierWithGroupSync();
    if ((remapId.x % 2 == 0) && (remapId.y % 2 == 0))
    {
        uint4 tileResult = 0;
        tileResult.x = sTileMarkerR[remapId.x][remapId.y];
        tileResult.y = sTileMarkerG[remapId.x][remapId.y];
        tileResult.z = sTileMarkerB[remapId.x][remapId.y];
        tileResult.w = sTileMarkerA[remapId.x][remapId.y];

        uint2 storePos = workGroupId * 4 + remapId / 2;
        rwTileMarker[storePos] = tileResult;
    }
}

[numthreads(64, 1, 1)]
void tilePrepareCS(uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    Texture2D<uint4> tileMarkerTexture = TBindless(Texture2D, uint4, pushConsts.markerTextureId);
    uint2 basePos = workGroupId * 16 + remap8x8(localThreadIndex);

    uint4 markerArray[4];
    [unroll(2)]
    for (uint y = 0; y < 2; y ++)
    {
        [unroll(2)]
        for (uint x = 0; x < 2; x ++)
        {
            uint2 samplePos = basePos + uint2(x, y) * 8;
            markerArray[x + y * 2] = tileMarkerTexture[samplePos];
        }
    }

    uint2 tile[4];
    uint tileCount = 0;
    [unroll(2)]
    for (uint y = 0; y < 2; y ++)
    {
        [unroll(2)]
        for (uint x = 0; x < 2; x ++)
        {
            uint2 samplePos = basePos + uint2(x, y) * 8;
            uint4 marker = markerArray[x + y * 2];

            const bool bExistMarker = (marker[pushConsts.markerIndex] & pushConsts.markerBit);
            const bool bAllInRange  = all(samplePos < pushConsts.markerDim);

            if (bExistMarker && bAllInRange)
            {
                tile[x + y * 2] = samplePos * 8;
                tileCount ++;
            }
            else
            {
                tile[x + y * 2].x = uint(~0);
            }
        }
    }

    [branch]
    if (tileCount > 0)
    {
        RWByteAddressBuffer rwCmdBuffer = RWByteAddressBindless(pushConsts.tileBufferCmdId);

        uint offset = 0;
        uint baseId = interlockedAddUint(pushConsts.tileBufferCountId, tileCount);

        [unroll(4)]
        for (uint i = 0; i < 4; i ++)
        {
            if (tile[i].x != ~0)
            {
                rwCmdBuffer.TypeStore(uint2, offset + baseId, tile[i]);
                offset ++;
            }
        }
    }
}

[numthreads(1, 1, 1)]
void prepareTileParamCS()
{
    const uint tileCount = BATL(uint, pushConsts.tileBufferCountId, 0);

    uint4 cmdParameter;
    cmdParameter.x = tileCount;
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;

    BATS(uint4, pushConsts.tileDrawCmdId, 0, cmdParameter);
}

#endif // !__cplusplus