#include "nanite.hlsli"

// BVH accelerate meshlet cull and lod selected. 

struct NaniteBVHPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(NaniteBVHPushConst);

    uint cameraViewId;
    uint switchFlags;


};
CHORD_PUSHCONST(NaniteBVHPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

[numthreads(64, 1, 1)]
void naniteBVHTraverseCS(uint threadId : SV_DispatchThreadID)
{

}

#endif // !__cplusplus