#ifndef SHADER_DDGI_CLIPMAP_UPDATE_HLSL
#define SHADER_DDGI_CLIPMAP_UPDATE_HLSL

#include "ddgi.h"

struct DDGIClipmapUpdatePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(DDGIClipmapUpdatePushConsts);

    int3 currentScrollOffset;
    uint cameraViewId;

    uint bClearAll;
    uint ddgiConfigBufferId;
    uint ddgiConfigId;
    uint probeTracedMarkUAV; 

    uint probeCounterUAV;
    uint probeUpdateLinearIndexUAV;
    uint probeUpdateMod;
    uint probeUpdateEqual;

    int  probeUpdateFrameThreshold;
    int  maxProbeUpdatePerFrame;
    uint cmdBufferId;
    uint probeCounterSRV;

    uint probeHistoryValidUAV;
    uint probeTraceFrameUAV;
};
CHORD_PUSHCONST(DDGIClipmapUpdatePushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli"
#include "bindless.hlsli"
#include "sample.hlsli"
#include "debug.hlsli"

// Only clear once when buffer create.
[numthreads(64, 1, 1)]
void clearMarkerBufferCS(uint linearProbeIndex : SV_DispatchThreadID)
{
    const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, pushConsts.ddgiConfigId);
    if (linearProbeIndex < ddgiConfig.getProbeCount())
    {
        // const int3 virtualProbeIndex = ddgiConfig.getVirtualVolumeId(linearProbeIndex);  
        // const int  physicsProbeLinearIndex = ddgiConfig.getPhysicalLinearVolumeId(virtualProbeIndex);

        // 
        BATS(int, pushConsts.probeTracedMarkUAV, linearProbeIndex, -1);
        BATS(int, pushConsts.probeHistoryValidUAV, linearProbeIndex, 0);
        BATS(int, pushConsts.probeTraceFrameUAV, linearProbeIndex, 0);
    } 
}

[numthreads(1, 1, 1)]
void indirectCmdParamCS()
{
    uint4 cmd;
    cmd.x = BATL(uint, pushConsts.probeCounterSRV, 1);
    cmd.y = kDDGIProbeRayTraceThreadGroupCount;
    cmd.z = 1;
    cmd.w = 1;
    BATS(uint4, pushConsts.cmdBufferId, 0, cmd);
}

[numthreads(64, 1, 1)]
void updateInvalidProbeTracePass_0_CS(uint dispatchId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // Get linear probe index. 
    const int linearProbeIndex = dispatchId;

    // Final result of probe allocate info.
    bool bNeedAllocate = false;
    int probeState = -1;

    // 
    int probeTraceFrame = 0;

    // 
    int  physicsProbeLinearIndex;
    bool bHistoryValid = false;

    // Load ddgi. 
    const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, pushConsts.ddgiConfigId);
    if (linearProbeIndex < ddgiConfig.getProbeCount())
    {
        // 
        const int3 virtualProbeIndex = ddgiConfig.getVirtualVolumeId(linearProbeIndex);  
        physicsProbeLinearIndex = ddgiConfig.getPhysicalLinearVolumeId(virtualProbeIndex);

        // If clear all, then all probe is invalid.
        bHistoryValid = !pushConsts.bClearAll;
        if (bHistoryValid)
        {
            const int3 physicsProbeIndex = ddgiConfig.getPhysicalVolumeId(virtualProbeIndex);

            // Query current probe state is out of bound or not because of scrolling.
            const int3 lastFramePhysicProbeIndex = virtualProbeIndex - pushConsts.currentScrollOffset;
            bHistoryValid = all(lastFramePhysicProbeIndex >= 0) && all(lastFramePhysicProbeIndex < ddgiConfig.probeDim);
        }

        if (bHistoryValid)
        {
            // Current frame history valid, but the probe may still no traced, so allocate if probestate is -1. 
            probeState = RWBATL(int, pushConsts.probeTracedMarkUAV, physicsProbeLinearIndex);

            probeTraceFrame = RWBATL(int, pushConsts.probeTraceFrameUAV, physicsProbeLinearIndex);
            check(probeTraceFrame > 0);

            // 
            if (probeState == -1)
            {
                probeTraceFrame = 0;
                bNeedAllocate = true;
            }
        }
        else
        {
            bNeedAllocate = true;
        }
    }

    // 
    uint allocatedCount_wave  = WaveActiveCountBits(bNeedAllocate);
    uint allocatedOffset_wave = WavePrefixCountBits(bNeedAllocate);

    // 
    uint allocatedStoreBaseId;
    if (WaveIsFirstLane())
    {
        allocatedStoreBaseId  = interlockedAddUint(pushConsts.probeCounterUAV, allocatedCount_wave, 0);
    }
    allocatedStoreBaseId = WaveReadLaneFirst(allocatedStoreBaseId);

    // 
    bool bAllocated = false;
    if (linearProbeIndex < ddgiConfig.getProbeCount())
    {
        if (bNeedAllocate)
        {
            uint id = allocatedStoreBaseId + allocatedOffset_wave;
            if (id < pushConsts.maxProbeUpdatePerFrame)
            {
                // Probe will trace current frame, so reset state. 
                probeState = 0;
                bAllocated = true;

                // Store trace info. 
                BATS(int, pushConsts.probeUpdateLinearIndexUAV, id, linearProbeIndex);
            }
            else
            {
                check(probeState == -1);
            }

            // Mark history no valid more.
            BATS(int, pushConsts.probeHistoryValidUAV, physicsProbeLinearIndex, 0);
        }
        else
        {
            check(probeState >= 0);
            probeState ++; // Frame increment. 
        }

        probeTraceFrame ++;
        BATS(int, pushConsts.probeTracedMarkUAV, physicsProbeLinearIndex, probeState);
        BATS(int, pushConsts.probeTraceFrameUAV, physicsProbeLinearIndex, probeTraceFrame);
    }

    uint finalAllocatedCount_wave  = WaveActiveCountBits(bAllocated);
    if (WaveIsFirstLane())
    {
        interlockedAddUint(pushConsts.probeCounterUAV, finalAllocatedCount_wave, 1);
    }
}

[numthreads(64, 1, 1)]
void updateInvalidProbeTracePass_1_CS(uint dispatchId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // Get linear probe index. 
    const int linearProbeIndex = dispatchId;

    // Final result of probe allocate info.
    bool bNeedAllocate = false;
    int  probeState = -1;
    int  physicsProbeLinearIndex;
    int  probeTraceFrame = 0;

    // Load ddgi. 
    const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, pushConsts.ddgiConfigId);
    if (linearProbeIndex < ddgiConfig.getProbeCount())
    {
        // 
        const int3 virtualProbeIndex = ddgiConfig.getVirtualVolumeId(linearProbeIndex);  
        physicsProbeLinearIndex = ddgiConfig.getPhysicalLinearVolumeId(virtualProbeIndex);

        // 
        probeState = RWBATL(int, pushConsts.probeTracedMarkUAV, physicsProbeLinearIndex);
        probeTraceFrame = RWBATL(int, pushConsts.probeTraceFrameUAV, physicsProbeLinearIndex);
        
        // Trace tick allocate add frame threshold reduce some probe update frequency. 
        check(pushConsts.probeUpdateFrameThreshold > 0);

        const bool bFillAllocate = (probeState > 0) && probeTraceFrame < ddgiConfig.probeFrameFill;

        if (probeState > pushConsts.probeUpdateFrameThreshold || bFillAllocate)
        {
            bNeedAllocate = (linearProbeIndex % pushConsts.probeUpdateMod) == pushConsts.probeUpdateEqual;
        }
    }

    // 
    uint allocatedCount_wave   = WaveActiveCountBits(bNeedAllocate);
    uint allocatedOffset_wave  = WavePrefixCountBits(bNeedAllocate);

    uint allocatedStoreBaseId;
    if (WaveIsFirstLane())
    {
        allocatedStoreBaseId = interlockedAddUint(pushConsts.probeCounterUAV, allocatedCount_wave, 0);
    }
    allocatedStoreBaseId = WaveReadLaneFirst(allocatedStoreBaseId);

    bool bAllocated = false;
    if (bNeedAllocate)
    {
        uint id = allocatedStoreBaseId + allocatedOffset_wave;
        if (id < pushConsts.maxProbeUpdatePerFrame)
        {
            BATS(int, pushConsts.probeUpdateLinearIndexUAV, id, linearProbeIndex);

            // 
            bAllocated = true;

            check(probeState > 0); // Don't repeat choose.
            BATS(int, pushConsts.probeTracedMarkUAV, physicsProbeLinearIndex, 0);
        }
    }

    uint finalAllocatedCount_wave  = WaveActiveCountBits(bAllocated);
    if (WaveIsFirstLane())
    {
        interlockedAddUint(pushConsts.probeCounterUAV, finalAllocatedCount_wave, 1);
    }
}

[numthreads(1, 1, 1)]
void copyValidCounterCS()
{
    uint x = RWBATL(uint, pushConsts.probeCounterUAV, 1);
    BATS(uint, pushConsts.probeCounterUAV, 0, x);
}

[numthreads(64, 1, 1)]
void appendRelightingProbeCountCS(uint dispatchId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    // Get linear probe index. 
    const int linearProbeIndex = dispatchId;

    // Final result of probe allocate info.
    bool bNeedAllocate = false;
    int  probeState = -1;
    int  physicsProbeLinearIndex;

    // Load ddgi. 
    const DDGIVoulmeConfig ddgiConfig = BATL(DDGIVoulmeConfig, pushConsts.ddgiConfigBufferId, pushConsts.ddgiConfigId);
    if (linearProbeIndex < ddgiConfig.getProbeCount())
    {
        // 
        const int3 virtualProbeIndex = ddgiConfig.getVirtualVolumeId(linearProbeIndex);  
        physicsProbeLinearIndex = ddgiConfig.getPhysicalLinearVolumeId(virtualProbeIndex);

        // 
        probeState = RWBATL(int, pushConsts.probeTracedMarkUAV, physicsProbeLinearIndex);
        if (probeState > pushConsts.probeUpdateFrameThreshold)
        {
            bNeedAllocate = (linearProbeIndex % pushConsts.probeUpdateMod) == pushConsts.probeUpdateEqual;
        }
    }

    // 
    uint allocatedCount_wave   = WaveActiveCountBits(bNeedAllocate);
    uint allocatedOffset_wave  = WavePrefixCountBits(bNeedAllocate);

    uint allocatedStoreBaseId;
    if (WaveIsFirstLane())
    {
        allocatedStoreBaseId = interlockedAddUint(pushConsts.probeCounterUAV, allocatedCount_wave, 0);
    } 
    allocatedStoreBaseId = WaveReadLaneFirst(allocatedStoreBaseId);  

    bool bAllocated = false;
    if (bNeedAllocate)
    {
        uint id = allocatedStoreBaseId + allocatedOffset_wave;
        if (id < pushConsts.maxProbeUpdatePerFrame) 
        {
            BATS(int, pushConsts.probeUpdateLinearIndexUAV, id, linearProbeIndex);

            // 
            bAllocated = true; // 
            check(probeState > 0); // Relighting probe require it already exist valid trace.
        }
    }

    uint finalAllocatedCount_wave  = WaveActiveCountBits(bAllocated);
    if (WaveIsFirstLane())
    {
        interlockedAddUint(pushConsts.probeCounterUAV, finalAllocatedCount_wave, 1);
    }
}


#endif // 

#endif // SHADER_DDGI_CLIPMAP_UPDATE_HLSL