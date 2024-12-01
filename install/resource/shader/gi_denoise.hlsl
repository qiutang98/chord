#include "gi.h"

struct GIDenoisePushConsts
{
    uint2 dim;

    bool firstFrame;
    uint srv;
    uint uav;
};
CHORD_PUSHCONST(GIDenoisePushConsts, pushConsts);

#ifndef __cplusplus

[numthreads(64, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    const uint2 gid = remap8x8(localThreadIndex); // [0x0 - 8x8)
    const uint2 tid = workGroupId * 8 + gid;
    if (any(tid >= pushConsts.dim))
    {
        // Out of bound pre-return. 
        return;
    }

    const float3 src = loadTexture2D_float3(pushConsts.srv, tid);
    const float3 dest = loadRWTexture2D_float3(pushConsts.uav, tid);

    float3 r = lerp(src, dest, pushConsts.firstFrame ? 0.0 : 0.99);

    storeRWTexture2D_float3(pushConsts.uav, tid, r);
}

#endif 