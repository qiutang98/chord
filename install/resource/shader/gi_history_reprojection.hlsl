#include "gi.h"

struct GIHistoryReprojectPushConsts
{
    uint2 gbufferDim;

    uint cameraViewId;
    uint motionVectorId;

    uint reprojectGIUAV;
    uint depthSRV;
    uint disoccludedMaskSRV;
    uint historyGISRV;
};
CHORD_PUSHCONST(GIHistoryReprojectPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const uint2 gid = remap8x8(localThreadIndex); 
    const uint2 tid = workGroupId * 8 + gid;

    if (any(tid >= pushConsts.gbufferDim))
    {
        // Out of bound pre-return.  
        return; 
    }

    float4 reprojectGI = 0.0;

    float deviceZ = loadTexture2D_float1(pushConsts.depthSRV, tid);
    if (deviceZ > 0.0)
    {
        float disoccludedMask = loadTexture2D_float1(pushConsts.disoccludedMaskSRV, tid);
        if (disoccludedMask < 1e-3f)
        {
            float2 pixel_uv = (tid + 0.5) / pushConsts.gbufferDim;
            float2 pixel_history_uv = pixel_uv + loadTexture2D_float2(pushConsts.motionVectorId, tid);
            if (all(pixel_history_uv >= 0.0) && all(pixel_history_uv <= 1.0))
            {
                reprojectGI = sampleTexture2D_float4(pushConsts.historyGISRV, pixel_history_uv, getPointClampEdgeSampler(perView));
            }
        }
    }

    storeRWTexture2D_float4(pushConsts.reprojectGIUAV, tid, reprojectGI);
}


#endif 