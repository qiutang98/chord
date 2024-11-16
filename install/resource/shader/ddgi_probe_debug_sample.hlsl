#include "ddgi.h"

struct DDGIDebugSamplePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(DDGIDebugSamplePushConsts);

    uint cameraViewId;
    uint ddgiConfigBufferId;
    uint ddgiCount;
    uint UAV;

    uint2 workDim;
    uint  normalRSId;
    uint depthTextureId;
};
CHORD_PUSHCONST(DDGIDebugSamplePushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli"
#include "bindless.hlsli"
#include "gltf.h"
#include "cascade_shadow_projection.hlsli"
#include "colorspace.h"

[numthreads(64, 1, 1)]  
void mainCS( 
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex) 
{   
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;
 
    uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);
    if (any(workPos >= pushConsts.workDim))
    {
        return;
    }

    float2 uv = (workPos + 0.5) / float2(pushConsts.workDim);
    const float1 deviceZ = loadTexture2D_float1(pushConsts.depthTextureId, workPos);

    float weightSum = 0.0;
    float3 irradianceWeights = 0.0;
    if (deviceZ > 0.0)
    {
        const float3 positionRS = getPositionRS(uv, max(deviceZ, kFloatEpsilon), perView); 

        float3 normalRS = loadTexture2D_float4(pushConsts.normalRSId, workPos).xyz * 2.0 - 1.0;
        normalRS = normalize(normalRS); 
 
        const float3 viewDirection = normalize(positionRS); 
        for (uint ddgiIndex = 0; ddgiIndex < pushConsts.ddgiCount; ddgiIndex ++) 
        {
            const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, ddgiIndex);
            float weight = ddgiConfig.getBlendWeight(positionRS);

            if (weightSum >= 1.0)
            { 
                break;
            }

            [branch] 
            if (weight <= 0.0)
            {
                continue;
            }
            else 
            {
                float3 surfaceBias = ddgiConfig.getSurfaceBias(normalRS, viewDirection);
                float3 radiance = ddgiConfig.sampleIrradiance(perView, positionRS, surfaceBias, normalRS);

                
                if (dot(radiance, 1.0) > kFloatEpsilon)
                {
                    float linearWeight = (weightSum > 0.0) ? (1.0 - weightSum) : weight;
                    irradianceWeights += radiance * linearWeight;
                    weightSum += linearWeight; 
                }
            }
        }  
    }

    float3 irradiance = 0.0; 
    if (weightSum > 0.0)
    {  
        irradiance = irradianceWeights / weightSum; 
    } 
    storeRWTexture2D_float3(pushConsts.UAV, workPos, irradiance);
}

#endif //!__cplusplus