#include "gi.h"

struct GISSAOPushConsts
{
    uint2 workDim;

    // 
    uint hzbSRV;
    
    uint cameraViewId;
    uint depthSRV;
    uint normalSRV;

    float uvRadius;
    float maxPixelScreenRadius;
    int stepCount;
    int sliceCount;

    float falloff_mul;
    float falloff_add;
    uint UAV;
}; 
CHORD_PUSHCONST(GISSAOPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "base.hlsli"  
#include "bindless.hlsli"
#include "blue_noise.hlsli"

// WARNING: HZB is 16 bit, not suitable for bent normal compute.
#define RAW_DEPTH_SAMPLE 1

float3 loadNormalRS(in const PerframeCameraView perView, int2 pos)
{
    float3 normalRS = loadTexture2D_float4(pushConsts.normalSRV, pos).xyz * 2.0 - 1.0;
    return normalize(normalRS);
}

float integrateHalfArc(float horizonAngle, float normalAngle)
{
    return (cos(normalAngle) + 2.f * horizonAngle * sin(normalAngle) - cos(2.f * horizonAngle - normalAngle)) / 4.f;
}

float3 integrateBentNormal(in const PerframeCameraView perView, float horizonAngle0, float horizonAngle1, float normalAngle, float3 viewDirRS, float3 sliceViewDir)
{
    float t0 = (6.0f * sin(horizonAngle0 - normalAngle) - sin(3.f * horizonAngle0 - normalAngle) +
                6.0f * sin(horizonAngle1 - normalAngle) - sin(3.f * horizonAngle1 - normalAngle) +
                16.f * sin(normalAngle) - 3.f * (sin(horizonAngle0 + normalAngle) + sin(horizonAngle1 + normalAngle))) / 12.f;
    float t1 = (-cos(3.f * horizonAngle0 - normalAngle)
                -cos(3.f * horizonAngle1 - normalAngle) +
                 8.f * cos(normalAngle) - 3.f * (cos(horizonAngle0 + normalAngle) + cos(horizonAngle1 + normalAngle))) / 12.f;

    float3 viewBentNormal  = float3(sliceViewDir.x * t0, sliceViewDir.y * t0, t1);
    float3 worldBentNormal = normalize(mul(perView.viewToTranslatedWorld, float4(viewBentNormal, 0.0)).xyz);

    //
    return mul(rotFromToMatrix(-perView.forwardRS, viewDirRS), worldBentNormal);
}



[numthreads(64, 1, 1)]
void mainCS(uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    SamplerState pointSampler = getPointClampEdgeSampler(perView);


    const int2 gid = remap8x8(localThreadIndex);
    const int2 tid = workGroupId * 8 + gid;

    if (any(tid >= pushConsts.workDim))
    {
        return; 
    }

    const float2 texelSize = 1.0f / pushConsts.workDim;
    const float2 uv = (tid + 0.5) * texelSize;
    const float maxDu = max(texelSize.x, texelSize.y);

    Texture2D<float> hzbTexture = TBindless(Texture2D, float, pushConsts.hzbSRV);

    
    const int2 hzbMip0Dim   = int2(perView.renderDimension.xy) >> 1;
    static const float kHZBMipOffset = 3.3; // 
    static const float kHZBMaxLevel  = 5.0;

#if RAW_DEPTH_SAMPLE
    const float deviceZ = loadTexture2D_float1(pushConsts.depthSRV, tid);
#else
    const float deviceZ = hzbTexture.Load(int3(uv * hzbMip0Dim, 0)); // loadTexture2D_float1(pushConsts.depthSRV, tid);
#endif

    if (deviceZ <= 0.0)
    {
        // Do nothing in sky pixel. 
        return;
    }

    const float3 normalRS = loadNormalRS(perView, tid);
    const float linearZ = perView.zNear / deviceZ;

    const float3 positionRS = getPositionRS(uv, deviceZ, perView); 
    const float3 viewDirRS = -normalize(positionRS);

    const float2 blueNoise2 = STBN_float2(scene.blueNoiseCtx, tid, scene.frameCounter);

    // Linear z scale uv radius, near view search large radius, far view search small radius. 
    // Exist one performance problem here:
    // When camera close to mesh, linear z will smaller, so slice uv radius will increase to a crazy value. 
    // We need to clamp it.
    float sliceUvRadius = pushConsts.uvRadius / linearZ; 

    // Clamp.
    sliceUvRadius = min(sliceUvRadius, maxDu * pushConsts.maxPixelScreenRadius);
    // 2 4 8 16 32 64 128 256
    // 0 1 2 3 4 5 6 7
    
    const int ssaoStepCount = pushConsts.stepCount;
    const int ssaoSliceCount = pushConsts.sliceCount;
    // 
    const float stepCountInverse = 1.0 / ssaoStepCount;
    const float sliceCountInverse = 1.0 / ssaoSliceCount;

    // 
    float ambientOcclusion = 0.0f;
    float3 bentNormal = 0.0;


    // 
    for (int sliceIndex = 0; sliceIndex < ssaoSliceCount; sliceIndex++)
    {
        float  sliceAngle  = ((sliceIndex + blueNoise2.x) * sliceCountInverse) * kPI;

        // 
        float  sliceCos;
        float  sliceSin;
        sincos(sliceAngle, sliceSin, sliceCos);

        //
        float2 sliceUvDir = float2(sliceCos, -sliceSin) * sliceUvRadius;

        //
        float3 sliceViewDir  = float3(sliceCos, sliceSin, 0.f);

        // 
        float3 sliceDirRS = normalize(mul(perView.viewToTranslatedWorld, float4(sliceViewDir, 0.0)).xyz);

        // 
        float3 orthoWorldDir = sliceDirRS - dot(sliceDirRS, viewDirRS) * viewDirRS;
        float3 projAxisDir   = normalize(cross(orthoWorldDir, viewDirRS));
        float3 projWorldNormal  = normalRS - dot(normalRS, projAxisDir) * projAxisDir;

        float  projWorldNormalLen   = length(projWorldNormal);
        float  projWorldNormalCos   = saturate(dot(projWorldNormal, viewDirRS) / projWorldNormalLen);
        float  projWorldNormalAngle = sign(dot(orthoWorldDir, projWorldNormal)) * acos(projWorldNormalCos);

        float  sideSigns[2] = { 1.f, -1.f};
        float  horizonAngles[2];



        for (int sideIndex = 0; sideIndex < 2; ++sideIndex)
        {
            float  horizon_min = cos(projWorldNormalAngle + sideSigns[sideIndex] * kPI * 0.5);
            float  horizonCos = horizon_min;

            for (int stepIndex = 0; stepIndex < ssaoStepCount; stepIndex++)
            {
                float  sampleStep = stepIndex * stepCountInverse;
                // Square distribution.
                sampleStep *= sampleStep;

                // Noise need still keep same pattern avoid destroy low bias feature.
                sampleStep += (blueNoise2.y + 1e-5f) * stepCountInverse; // 1e-5 avoid self intersection. 

                float2 sampleUvOffset = sampleStep * sliceUvDir;
                float2 sampleUv = uv + sideSigns[sideIndex] * sampleUvOffset;

                // Cache Miss 
            #if RAW_DEPTH_SAMPLE
                float sampleDepth = sampleTexture2D_float1(pushConsts.depthSRV, sampleUv, pointSampler);
                //
            #else
                int mipLevel = clamp(log2(length(sampleUvOffset * hzbMip0Dim)) - kHZBMipOffset, 0.0, kHZBMaxLevel);
                int2 hzbMip0Coord = sampleUv * hzbMip0Dim; // Sample pos in hzb mip 0.
                int2 hzbMipCoord = hzbMip0Coord >> mipLevel;
                float sampleDepth =  hzbTexture.Load(int3(hzbMipCoord, mipLevel));
            #endif

                float3 samplePositionRS = getPositionRS(sampleUv, sampleDepth, perView); 

                // 
                float3 horizonWorldDir = samplePositionRS - positionRS;
                float horizonWorldLen = length(horizonWorldDir); 
                horizonWorldDir /= horizonWorldLen;

                // 
                float sampleWeight = saturate(horizonWorldLen * pushConsts.falloff_mul + pushConsts.falloff_add);
 
                // 
                float sampleCos = lerp(horizon_min, dot(horizonWorldDir, viewDirRS), sampleWeight);
                horizonCos = max(horizonCos, sampleCos); 
            }

            // Ambient Occlusion
            horizonAngles[sideIndex] = sideSigns[sideIndex] * acos(horizonCos);
            ambientOcclusion += projWorldNormalLen * integrateHalfArc(horizonAngles[sideIndex], projWorldNormalAngle);
        }

        // Bent normal
        bentNormal += projWorldNormalLen * integrateBentNormal(perView, horizonAngles[0], horizonAngles[1], projWorldNormalAngle, viewDirRS, sliceViewDir);
    }

    // AO. 
    ambientOcclusion *= sliceCountInverse;

    // Bent normal. 
    bentNormal = normalize(bentNormal) * 0.5 + 0.5; // remap to [0, 1]
    storeRWTexture2D_float4(pushConsts.UAV, tid, float4(bentNormal, ambientOcclusion));          
}

#endif 