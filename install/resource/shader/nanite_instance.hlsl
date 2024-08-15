#include "nanite.hlsli"

// Instance culling for nanite. 

struct NaniteInstanceCullingPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(NaniteInstanceCullingPushConst);

    uint cameraViewId;
    uint switchFlags;


};
CHORD_PUSHCONST(NaniteInstanceCullingPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void naniteInstanceCullingCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;  

    // Nanite instance culling work on all gltf object in scene. 
    if (threadId >= scene.GLTFObjectCount)
    {
        return;
    } 

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, threadId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);

    const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    const float4x4 localToClip = mul(perView.translatedWorldToClip, localToTranslatedWorld);

    const float3 posMin    = primitiveInfo.posMin;
    const float3 posMax    = primitiveInfo.posMax;
    const float3 posCenter = (posMin + posMax) * 0.5;
    const float3 extent    = posMax - posCenter;

    // Frustum visible culling: use obb.
    if (shaderHasFlag(pushConsts.switchFlags, kFrustumCullingEnableBit))
    {
        if (frustumCulling(perView.frustumPlane, posCenter, extent, localToTranslatedWorld))
        {
            return;
        }
    }

    // Object still visible after frustum culling. 
    {
        // Start culling from top of cluster group. 
    }
}


#endif // !__cplusplus