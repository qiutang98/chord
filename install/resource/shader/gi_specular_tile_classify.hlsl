#include "gi.h"

struct GISpecularTracePushConsts
{
    uint2 gbufferDim;

    uint cameraViewId;
    uint roughnessId;
    uint depthId;
};
CHORD_PUSHCONST(GISpecularTracePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint2 gid = remap8x8(localThreadIndex); 
    const uint2 tid = workGroupId * 8 + gid;

    float roughness = loadTexture2D_float1(pushConsts.roughnessId, tid);
    float depth = loadTexture2D_float1(pushConsts.depthId, tid);

    const bool bAllInScreen = all(tid < pushConsts.gbufferDim);

    // When roughness too high will sample screen probe as fallback. 
    // Which help us reduce noise. 
    const bool bCanReflect = (depth > 0.0) && (roughness < kGIGlossyMaxRoughness);
    const bool bPerfectMirror = (roughness < kGIPerfectMirrorMaxRoughness);

    // 2x2 Quad share one ray for glossy surface. 
    uint quadIndex = WaveGetLaneIndex() % 4;
    const bool bBaseRay = WaveGet

}


#endif