#ifndef SHADER_GI_SPATIAL_FILTER_DIFFUSE_HLSL
#define SHADER_GI_SPATIAL_FILTER_DIFFUSE_HLSL

#include "gi.h"

struct GISpatialFilterPushConsts
{
    uint2 gbufferDim;
    int2 direction;

    uint cameraViewId; 
    uint depthSRV;
    uint originSRV;
    uint SRV;
    uint UAV;

    uint normalRS;
};
CHORD_PUSHCONST(GISpatialFilterPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

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
        // Out of bound pre-return.  
        return; 
    }

    float deviceZ = loadTexture2D_float1(pushConsts.depthSRV, tid);
    if (deviceZ <= 0.0)
    {
        storeRWTexture2D_float4(pushConsts.UAV, tid, 0.0);
        return;
    }
    const float2 texelSize = 1.0 / pushConsts.gbufferDim;

    float4 centerDiffuse =  loadTexture2D_float4(pushConsts.SRV, tid);

    float3 centerNormalRS = loadTexture2D_float4(pushConsts.normalRS, tid).xyz * 2.0 - 1.0;
    centerNormalRS = normalize(centerNormalRS);

    float2 uv = (tid + 0.5) * texelSize;
    float3 centerPositionRS =  getPositionRS(uv, max(deviceZ, kFloatEpsilon), perView); 

    float distanceScaleFactor = max(1e-3f, length(centerPositionRS));
    int filteredRadius = loadTexture2D_float2(pushConsts.originSRV, tid).x * 8.0;
    if (filteredRadius == 0)
    {
        storeRWTexture2D_float4(pushConsts.UAV, tid, centerDiffuse);
    }
    else
    {
        float4 accumulateColor = 0.0;
        float accumulateWeight = 0.0;
        for (int i = -filteredRadius; i <= filteredRadius; i ++)
        {
            int2 sampleCoord = tid + pushConsts.direction * i;
            if (any(sampleCoord < 0) || any(sampleCoord >= pushConsts.gbufferDim))
            {
                continue;
            }

            float sampleDeviceZ = loadTexture2D_float1(pushConsts.depthSRV, sampleCoord);
            if (sampleDeviceZ <= 0.0)
            {
                continue;
            }

            float4 sampleColor = loadTexture2D_float4(pushConsts.SRV, sampleCoord);

            float2 sampleUv = (sampleCoord + 0.5) * texelSize;
            float3 samplePositionRS =  getPositionRS(sampleUv, max(sampleDeviceZ, kFloatEpsilon), perView); 

            float3 sampleNormalRS = loadTexture2D_float4(pushConsts.normalRS, sampleCoord).xyz * 2.0 - 1.0;
            sampleNormalRS = normalize(sampleNormalRS);

            float normalFactor = pow(saturate(dot(centerNormalRS, sampleNormalRS)), 16.0);
            float distanceFactor = saturate(1.0 - length(samplePositionRS - centerPositionRS) / distanceScaleFactor);
            float weight = pow(normalFactor * distanceFactor, 16.0);

            if (any(sampleColor > 1e-6f))
            {
                accumulateWeight += weight;
                accumulateColor += sampleColor * weight;
            }
        }

        accumulateColor /= max(1e-6f, accumulateWeight);
        if (any(isnan(accumulateColor))) 
        {
            accumulateColor = 0.0;
        }

        storeRWTexture2D_float4(pushConsts.UAV, tid, accumulateColor);
    }
}
#endif 

#endif // SHADER_GI_SPATIAL_FILTER_DIFFUSE_HLSL