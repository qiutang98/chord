#include "base.h"

struct TSRPreparePushConsts
{
    uint2 gbufferDim;
    uint cameraViewId; 
    uint depthSRV;

    uint motionVectorSRV;
    uint UAV;
};
CHORD_PUSHCONST(TSRPreparePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "base.hlsli"
#include "bindless.hlsli"

static const int2 kCrossSamplex4[4] = 
{
    int2( 1,  1),
    int2(-1, -1),
    int2( 1, -1),
    int2(-1,  1),
};

// 
[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const int2 gid = remap8x8(localThreadIndex); 
    const int2 tid = workGroupId * 8 + gid;

    if (any(tid >= pushConsts.gbufferDim))
    {
        return;
    }

    float3 closestDeviceZ = 0.0;

    [unroll(4)]
    for (uint i = 0; i < 4; i ++)
    {
        float2 sampleCoord = clamp(tid + kCrossSamplex4[i], 0, pushConsts.gbufferDim - 1);
        float deviceZ = loadTexture2D_float1(pushConsts.depthSRV, sampleCoord);
        closestDeviceZ = closestDeviceZ.z < deviceZ ? float3(sampleCoord, deviceZ) : closestDeviceZ;
    }

    float2 motionVector = 0.0;
    if (closestDeviceZ.z > 0.0)
    {
        motionVector = loadTexture2D_float2(pushConsts.motionVectorSRV, closestDeviceZ.xy);
    }
    else
    {
        // Sky area replace with camera virtual motion vector. 
        float2 pixel_uv = (tid + 0.5) / pushConsts.gbufferDim;
        const float3 positionRS = getPositionRS(pixel_uv, kFloatEpsilon, perView); 

        // 
        const float3 positionRS_LastFrame = positionRS + float3(perView.cameraWorldPos.getDouble3() - perView.cameraWorldPosLastFrame.getDouble3());
        const float3 UVz_LastFrame = projectPosToUVz(positionRS_LastFrame, perView.translatedWorldToClipLastFrame);

        motionVector = UVz_LastFrame.xy - pixel_uv;
    }

    storeRWTexture2D_float2(pushConsts.UAV, tid, motionVector);
}

#endif 