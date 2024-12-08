#include "base.h"

struct AutoExposurePushConsts
{

};
CHORD_PUSHCONST(AutoExposurePushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void mainCS(
    uint workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    
}


#endif 