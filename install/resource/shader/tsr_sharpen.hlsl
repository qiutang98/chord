#ifndef SHADER_TSR_SHARPEN_HLSL
#define SHADER_TSR_SHARPEN_HLSL

#include "base.h"

struct TSRSharpenPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(TSRSharpenPushConsts);

    uint2 gbufferDim;
    
    float sharpeness;
    uint cameraViewId;
    uint SRV;
    uint UAV;
};
CHORD_PUSHCONST(TSRSharpenPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "base.hlsli"
#include "bindless.hlsli"

groupshared float sR[18][18];
groupshared float sG[18][18];
groupshared float sB[18][18];

float3 ldsLoadColor(int2 xy)
{
    xy ++;
    return float3(sR[xy.x][xy.y], sG[xy.x][xy.y], sB[xy.x][xy.y]);
}

[numthreads(256, 1, 1)]
void mainCS(
    uint2 workGroupId : SV_GroupID, uint localThreadIndex : SV_GroupIndex)
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId); 
    const GPUBasicData scene = perView.basicData;

    const int2 gid = remap16x16(localThreadIndex);
    const int2 tid = workGroupId * 16 + gid;

    for (int y = gid.y; y < 18; y += 16)
    {
        for (int x = gid.x; x < 18; x += 16)
        {
            int2 sampleCoord = clamp(workGroupId * 16 + int2(x, y) - 1, 0, pushConsts.gbufferDim - 1);
            float3 color = loadTexture2D_float3(pushConsts.SRV, sampleCoord);
            color = fastTonemap(color);

            sR[x][y] = color.x;
            sG[x][y] = color.y;
            sB[x][y] = color.z;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // a b c
    // d e f
    // g h i
    float3 a = ldsLoadColor(gid + int2(-1, -1));
    float3 b = ldsLoadColor(gid + int2( 0, -1));
    float3 c = ldsLoadColor(gid + int2( 1, -1));
    float3 d = ldsLoadColor(gid + int2(-1,  0));
    float3 e = ldsLoadColor(gid + int2( 0,  0));
    float3 f = ldsLoadColor(gid + int2( 1,  0)); 
    float3 g = ldsLoadColor(gid + int2(-1,  1));
    float3 h = ldsLoadColor(gid + int2( 0,  1));
    float3 i = ldsLoadColor(gid + int2( 1,  1));


    float mnR = min3(min3(d.r,e.r,f.r),b.r,h.r);
    float mnG = min3(min3(d.g,e.g,f.g),b.g,h.g);
    float mnB = min3(min3(d.b,e.b,f.b),b.b,h.b);

    float mnR2 = min3(min3(mnR,a.r,c.r),g.r,i.r);
    float mnG2 = min3(min3(mnG,a.g,c.g),g.g,i.g);
    float mnB2 = min3(min3(mnB,a.b,c.b),g.b,i.b);

    mnR = mnR + mnR2;
    mnG = mnG + mnG2;
    mnB = mnB + mnB2;

    float mxR = max3(max3(d.r,e.r,f.r),b.r,h.r);
    float mxG = max3(max3(d.g,e.g,f.g),b.g,h.g);
    float mxB = max3(max3(d.b,e.b,f.b),b.b,h.b);

    float mxR2 = max3(max3(mxR,a.r,c.r),g.r,i.r);
    float mxG2 = max3(max3(mxG,a.g,c.g),g.g,i.g);
    float mxB2 = max3(max3(mxB,a.b,c.b),g.b,i.b);

    mxR = mxR + mxR2;
    mxG = mxG + mxG2;
    mxB = mxB + mxB2;

    float rcpMR = 1.0f / mxR;
    float rcpMG = 1.0f / mxG;
    float rcpMB = 1.0f / mxB;

    float ampR = clamp(min(mnR, 2.0f - mxR) * rcpMR, 0.0f, 1.0f);
    float ampG = clamp(min(mnG, 2.0f - mxG) * rcpMG, 0.0f, 1.0f);
    float ampB = clamp(min(mnB, 2.0f - mxB) * rcpMB, 0.0f, 1.0f);

    // Shaping amount of sharpening.
    ampR = sqrt(ampR);
    ampG = sqrt(ampG);
    ampB = sqrt(ampB);

    // Filter shape.
    //  0 w 0
    //  w 1 w
    //  0 w 0
    float peak = - 1.0f / lerp(8.0f, 5.0f, clamp(pushConsts.sharpeness, 0.0f, 1.0f));
    float wR = ampR * peak;
    float wG = ampG * peak;
    float wB = ampB * peak;

    float rcpWeightR = 1.0f / (1.0f + 4.0f * wR);
    float rcpWeightG = 1.0f / (1.0f + 4.0f * wG);
    float rcpWeightB = 1.0f / (1.0f + 4.0f * wB);

    float3 outColor;

    outColor.r = clamp((b.r * wR + d.r * wR + f.r * wR + h.r * wR + e.r) * rcpWeightR, 0.0f, 1.0f);
    outColor.g = clamp((b.g * wG + d.g * wG + f.g * wG + h.g * wG + e.g) * rcpWeightG, 0.0f, 1.0f);
    outColor.b = clamp((b.b * wB + d.b * wB + f.b * wB + h.b * wB + e.b) * rcpWeightB, 0.0f, 1.0f);

    outColor = fastTonemapInvert(outColor);
    storeRWTexture2D_float3(pushConsts.UAV, tid, outColor);
}


#endif 

#endif // SHADER_TSR_SHARPEN_HLSL