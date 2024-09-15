#include "base.h"

struct VSMPreparePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(VSMPreparePushConsts);

    uint cameraViewId;
    uint depthId;

};
CHORD_PUSHCONST(VSMPreparePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.


[numthreads(64, 1, 1)]
void markUsedVirtualTile(uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex); 
    if (any(workPos >= uint2(perView.renderDimension.xy)))
    {
        return;
    }

    const float2 fragCoord = workPos + 0.5;
    const float2 uv = fragCoord * perView.renderDimension.zw;

    Texture2D<float> depthTexture = TBindless(Texture2D, float, pushConsts.depthId);
    const float deviceZ = depthTexture[workPos];
    if (deviceZ <= 0.0)
    {
        // Skip sky area. 
        return; 
    }

    // Relative to camera world position.
    const float3 positionRS = getPositionRS(uv, deviceZ, perView);

    // Now get world space position.
    const double3 positionWS = double3(positionRS) +  asDouble3(perView.cameraWorldPos);

}

#endif // !__cplusplus