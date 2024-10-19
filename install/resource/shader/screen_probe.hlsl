#include "base.h"

#define kGIScreenProbeSize 8

struct ScreenProbePushConsts
{
    uint2 workDim;
    uint cameraViewId;

    uint depthId;
    uint normalId;

    uint randomSeed
};
CHORD_PUSHCONST(ScreenProbePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

static const uint kSpawnScreenProbeTestCount = kGIScreenProbeSize;
static const uint kSpawnScreenProbeMaxCount  = kGIScreenProbeSize * kGIScreenProbeSize;

struct ScreenProbeSpawnInfo
{
    uint2  samplePos;

    float3 normalRS;
    float1 deviceZ;


};

ScreenProbeSpawnInfo defaultScreenProbeSpawnInfo()
{
    ScreenProbeSpawnInfo result;

    result.samplePos = kUnvalidIdUint32;
    result.normalRS  = 0;
    result.deviceZ   = 0;


    return result;
}

[numthreads(64, 1, 1)]
void spawnScreenProbesCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint2 workPos = workGroupId * 8 + remap8x8(localThreadIndex);
    if (any(workPos >= pushConsts.workDim))
    {
        return;
    }

    const uint2 probeBasePos = workPos * kGIScreenProbeSize; // One probe handle 8x8 area. 

    // [isolate]
    {
        uint1 seed = 0xFF & pcgHash(scene.frameCounter + pcgHash(probeBasePos.x + pcgHash(probeBasePos.y)));
        storeRWTexture2D_uint1(pushConsts.randomSeed, workPos, seed);
    }

    const uint linearProbeId = workPos.x + workPos.y * pushConsts.workDim.x;
    const uint frameCounterSeed = pcgHash(scene.frameCounter);

    float deviceZ = 0.0;
    uint2 samplePos = kUnvalidIdUint32;

    // Random sample multi times to find one non empty area pixel. 
    for (uint i = 0; i < kSpawnScreenProbeTestCount; i ++)
    {
        uint seed = frameCounterSeed + pcgHash(probeBasePos.x + pcgHash(probeBasePos.y + pcgHash(i)));
        seed &= (kSpawnScreenProbeMaxCount - 1);
        const float2 hammersleySeed = hammersley2d(seed, kSpawnScreenProbeMaxCount);

        //
        samplePos = probeBasePos + uint2(floor(kGIScreenProbeSize * hammersleySeed));
        deviceZ = loadTexture2D_float(pushConsts.depthId, samplePos);

        // Skip empty area. otherwise break loop.
        if (deviceZ <= 0.0) { continue; } else { break; }
    }

    bool bProbeValid = any(samplePos != kUnvalidIdUint32) && (deviceZ > 0.0);

    ScreenProbeSpawnInfo probeInfo = defaultScreenProbeSpawnInfo();
    if (bProbeValid)
    {

    }
}

[numthreads(64, 1, 1)]
void 


#endif 