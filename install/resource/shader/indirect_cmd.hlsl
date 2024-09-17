#include "base.h"

struct IndirectDispatchCmdPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(IndirectDispatchCmdPushConsts);

    uint groupSize;
    uint countBufferId;
    uint cmdBufferId; // Require uvec4 format.
    uint offset;      // 
};
CHORD_PUSHCONST(IndirectDispatchCmdPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"

[numthreads(1, 1, 1)]
void indirectCmdParamCS()
{
    const uint count = BATL(uint, pushConsts.countBufferId, 0);

    uint4 cmdParameter;
    cmdParameter.x = (count + pushConsts.groupSize - 1) / pushConsts.groupSize;
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;
    BATS(uint4, pushConsts.cmdBufferId, pushConsts.offset, cmdParameter);
}

#endif