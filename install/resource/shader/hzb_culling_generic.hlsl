#include "gltf.h"

// Two phase stage for main view culling.
// HZB sample 4x4 in higher level for better culling state.

struct HZBCullingGenericPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(HZBCullingGenericPushConst);

    uint cameraViewId;
    uint instanceViewId;
    uint instanceViewOffset;
    uint bObjectUseLastFrameProject;
    uint switchFlags;

    uint  hzb; // hzb texture id.  

    uint drawedMeshletCountId;
    uint drawedMeshletCmdId;
    uint drawedMeshletCountId_1;
    uint drawedMeshletCmdId_1;
};
CHORD_PUSHCONST(HZBCullingGenericPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"
#include "debug_line.hlsli"
#include "nanite_shared.hlsli"

[numthreads(64, 1, 1)]
void mainCS(uint threadId : SV_DispatchThreadID)
{
    const InstanceCullingViewInfo instanceView = BATL(InstanceCullingViewInfo, pushConsts.instanceViewId, pushConsts.instanceViewOffset);

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

    //
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData); 
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    const float3 posMin    = meshlet.posMin;
    const float3 posMax    = meshlet.posMax;
    const float3 posCenter = (posMin + posMax) * 0.5;
    const float3 extent    = posMax - posCenter;

    bool bVisible = true;
    if (shaderHasFlag(pushConsts.switchFlags, kHZBCullingEnableBit))
    {
        float3 maxUVz = -10.0;
        float3 minUVz =  10.0;


        float4x4 localToTranslatedWorld = pushConsts.bObjectUseLastFrameProject != 0 ? objectInfo.basicData.localToTranslatedWorldLastFrame : objectInfo.basicData.localToTranslatedWorld;

        float3 relativeOffset = float3(asDouble3(perView.cameraWorldPos) - asDouble3(instanceView.cameraWorldPos));
        localToTranslatedWorld[0][3] += relativeOffset.x;
        localToTranslatedWorld[1][3] += relativeOffset.y;
        localToTranslatedWorld[2][3] += relativeOffset.z;

        const float4x4 mvp = mul(instanceView.translatedWorldToClip, localToTranslatedWorld);

        for (uint i = 0; i < 8; i ++)
        {
            const float3 extentPos = posCenter + extent * kExtentApplyFactor[i];
            const float3 UVz = projectPosToUVz(extentPos, mvp);
            minUVz = min(minUVz, UVz);
            maxUVz = max(maxUVz, UVz);
        }

        if (bVisible && all(minUVz < 1.0) && all(maxUVz > 0.0)) // No cross near or far plane.
        {
            minUVz.xy = saturate(minUVz.xy);
            maxUVz.xy = saturate(maxUVz.xy);

            const float4 uvRect = float4(minUVz.xy, maxUVz.xy);

            // offset half pixel make box tight.
            int4 pixelRect = int4(uvRect * instanceView.renderDimension.xyxy);

            // Range clamp.
            pixelRect.xy = max(0, pixelRect.xy);
            pixelRect.zw = min(instanceView.renderDimension.xy - 1, pixelRect.zw);

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
                const int2 hzbMip0Dim   = int2(instanceView.renderDimension.xy) >> 1;

                // Get mip level. 
                const int2 mipLevels = firstbithigh(hzbMip0Coord.zw - hzbMip0Coord.xy);
                
                // 2x2 sample bias one level.
                int mipLevel  = max(0, max(mipLevels.x, mipLevels.y));
                    mipLevel += any((hzbMip0Coord.zw >> mipLevel) - (hzbMip0Coord.xy >> mipLevel) >= 2) ? 1 : 0;

                // Get selected hzb level coord.
                const int4 hzbMipCoord = hzbMip0Coord >> mipLevel;
                const int2 hzbMipDim   = hzbMip0Dim   >> mipLevel;

                // Load min z in 4x4 pattern.
                float zMin = 10.0f;
                Texture2D<float> hzbTexture = TBindless(Texture2D, float, pushConsts.hzb);
                [unroll(2)]
                for (int x = 0; x < 2; x ++)
                {
                    [unroll(2)]
                    for(int y = 0; y < 2; y ++)
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

    uint visibleStoreBaseId;
    uint unvisibleStoreBaseId;
    if (WaveIsFirstLane())
    {
        visibleStoreBaseId   = interlockedAddUint(pushConsts.drawedMeshletCountId_1, visibleCount);
    }

    visibleStoreBaseId   = WaveReadLaneFirst(visibleStoreBaseId);

    // If visible, add to draw list.
    if (bVisible)
    {
        uint drawCmdId = visibleStoreBaseId + visibleOffsetId;
        BATS(uint3, pushConsts.drawedMeshletCmdId_1, drawCmdId, drawCmd);
    }
}

#endif // !__cplusplus