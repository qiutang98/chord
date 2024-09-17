#include "gltf.h"

// Two phase stage for main view culling.
// HZB sample 4x4 in higher level for better culling state.

struct HZBCullingPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(HZBCullingPushConst);

    uint cameraViewId;
    uint switchFlags;

    uint  hzb; // hzb texture id.  
    uint  hzbMipCount;
    float uv2HzbX;
    float uv2HzbY;
    uint  hzbMip0Width;
    uint  hzbMip0Height;

    uint drawedMeshletCountId;
    uint drawedMeshletCmdId;
    uint drawedMeshletCountId_1;
    uint drawedMeshletCmdId_1;
    uint drawedMeshletCountId_2;
    uint drawedMeshletCmdId_2;
};
CHORD_PUSHCONST(HZBCullingPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"
#include "debug_line.hlsli"
#include "nanite_shared.hlsli"

[numthreads(64, 1, 1)]
void hzbMainViewCullingCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);
    if (threadId >= meshletCount)
    {
        return;
    }

    const uint3 drawCmd = BATL(uint3, pushConsts.drawedMeshletCmdId, threadId);

    uint objectId  = drawCmd.x;
    uint meshletId = drawCmd.y;

#if DIM_HZB_CULLING_PHASE_0
    check(drawCmd.z == threadId);
#endif 

    //
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData); 
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    const float3 posMin = meshlet.posMin;
    const float3 posMax = meshlet.posMax;
    const float3 posCenter = (posMin + posMax) * 0.5;
    const float3 extent = posMax - posCenter;

    bool bVisible = true;
    if (shaderHasFlag(pushConsts.switchFlags, kHZBCullingEnableBit))
    {
        float3 maxUVz = -10.0;
        float3 minUVz =  10.0;

        const float4x4 mvp = 
        #if DIM_HZB_CULLING_PHASE_0
            // Project to prev frame and do some occlusion culling. 
            mul(perView.translatedWorldToClipLastFrame, objectInfo.basicData.localToTranslatedWorldLastFrame);
        #else
            mul(perView.translatedWorldToClip, localToTranslatedWorld);
        #endif

        for (uint i = 0; i < 8; i ++)
        {
            const float3 extentPos = posCenter + extent * kExtentApplyFactor[i];
            const float3 UVz = projectPosToUVz(extentPos, mvp);
            minUVz = min(minUVz, UVz);
            maxUVz = max(maxUVz, UVz);
        }

        const bool zInRange = maxUVz.z < 1.0f && minUVz.z > 0.0f;

        // Frustum culling.
        if (zInRange)
        {
            if (any(minUVz.xy >= 1) || any(maxUVz.xy <= 0))
            {
                bVisible = false;
            }
        }

        if (bVisible && zInRange) // No cross near or far plane.
        {
            minUVz.xy = saturate(minUVz.xy);
            maxUVz.xy = saturate(maxUVz.xy);

            const float4 uvRect = float4(minUVz.xy, maxUVz.xy);

            // offset half pixel make box tight.
            int4 pixelRect = int4(uvRect * perView.renderDimension.xyxy + float4(0.5, 0.5, -0.5, -0.5));

            // Range clamp.
            pixelRect.xy = max(0, pixelRect.xy);
            pixelRect.zw = min(perView.renderDimension.xy - 1, pixelRect.zw);



            // 
            if (any(pixelRect.zw < pixelRect.xy))
            {
                // Zero area just culled.
                bVisible = false;
            } 
            else
            {
                // Cast to mip0.
                const int4 hzbMip0Coord = pixelRect >> 1;
                const int2 hzbMip0Dim   = int2(perView.renderDimension.xy) >> 1;

                // Get mip level. 
                const int2 mipLevels = firstbithigh(hzbMip0Coord.zw - hzbMip0Coord.xy);
                
                // 4x4 sample bias one level.
                const int mipOffset = 1;
                int mipLevel  = max(0, max(mipLevels.x, mipLevels.y) - mipOffset);
                    mipLevel += any((hzbMip0Coord.zw >> mipLevel) - (hzbMip0Coord.xy >> mipLevel) >= 4) ? 1 : 0;

                // Get selected hzb level coord.
                const int4 hzbMipCoord = hzbMip0Coord >> mipLevel;
                const int2 hzbMipDim   = hzbMip0Dim   >> mipLevel;

                // Load min z in 4x4 pattern.
                float zMin = 10.0f;
                Texture2D<float> hzbTexture = TBindless(Texture2D, float, pushConsts.hzb);
                [unroll(4)]
                for (int x = 0; x < 4; x ++)
                {
                    [unroll(4)]
                    for(int y = 0; y < 4; y ++)
                    {
                        int2 sampleCoord = min(hzbMipCoord.zw, int2(x, y) + hzbMipCoord.xy);
                        zMin = min(zMin, hzbTexture.Load(int3(sampleCoord, mipLevel)));
                    }
                }
                
                if (zMin > maxUVz.z)
                {
                    // Occluded by hzb.
                    bVisible = false;
                }
            }
        }
    }

    const uint visibleCount    = WaveActiveCountBits(bVisible);
    const uint visibleOffsetId = WavePrefixCountBits(bVisible);

#if DIM_HZB_CULLING_PHASE_0
    const bool bUnVisibile = !bVisible;
    const uint unVisibleCount    = WaveActiveCountBits(bUnVisibile);
    const uint unVisibleOffsetId = WavePrefixCountBits(bUnVisibile);
#endif

    uint visibleStoreBaseId;
    uint unvisibleStoreBaseId;
    if (WaveIsFirstLane())
    {
        visibleStoreBaseId   = interlockedAddUint(pushConsts.drawedMeshletCountId_1, visibleCount);
#if DIM_HZB_CULLING_PHASE_0
        unvisibleStoreBaseId = interlockedAddUint(pushConsts.drawedMeshletCountId_2, unVisibleCount);
#endif
    }

    visibleStoreBaseId   = WaveReadLaneFirst(visibleStoreBaseId);
#if DIM_HZB_CULLING_PHASE_0
    unvisibleStoreBaseId = WaveReadLaneFirst(unvisibleStoreBaseId);
#endif

    // If visible, add to draw list.
    if (bVisible)
    {
        uint drawCmdId = visibleStoreBaseId + visibleOffsetId;
        BATS(uint3, pushConsts.drawedMeshletCmdId_1, drawCmdId, drawCmd);

    #if DIM_PRINT_DEBUG_BOX
        uint packColor = simpleHashColorPack(meshletId);
        LineDrawVertex worldPosExtents[8];
        for (uint i = 0; i < 8; i ++)
        {
            const float3 extentPos = posCenter + extent * kExtentApplyFactor[i];
            const float3 extentPosRS = mul(localToTranslatedWorld, float4(extentPos, 1.0)).xyz;
            worldPosExtents[i].translatedWorldPos = extentPosRS;
            worldPosExtents[i].color = packColor;
        }
        addBox(scene, worldPosExtents);
    #endif
    }
    #if DIM_HZB_CULLING_PHASE_0
    else
    {
        uint drawCmdId = unvisibleStoreBaseId + unVisibleOffsetId;
        BATS(uint3, pushConsts.drawedMeshletCmdId_2, drawCmdId, drawCmd);
    }
    #endif
}

#endif // !__cplusplus