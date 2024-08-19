#ifndef SHADER_BASE_HLSLI
#define SHADER_BASE_HLSLI

#include "base.h"

#define kPI 3.14159265358979323846
#define kInvertPI (1.0 / kPI)
#define KPIOver2  (kPI / 2.0)
#define KPIOver4  (kPI / 4.0)

/**
    Gather pattern

    https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/gather4--sm5---asm-

    This instruction behaves like the sample instruction, but a filtered sample is not generated. 
    The four samples that would contribute to filtering are placed into xyzw in counter clockwise order 
    starting with the sample to the lower left of the queried location. 
    
    This is the same as point sampling with (u,v) texture coordinate deltas at the following locations: 
    (-,+),(+,+),(+,-),(-,-), where the magnitude of the deltas are always half a texel.

**/

// Simple hash uint.
// from niagara stream. see https://www.youtube.com/watch?v=BR2my8OE1Sc
uint simpleHash(uint a)
{
   a = (a + 0x7ed55d16) + (a << 12);
   a = (a ^ 0xc761c23c) ^ (a >> 19);
   a = (a + 0x165667b1) + (a << 5);
   a = (a + 0xd3a2646c) ^ (a << 9);
   a = (a + 0xfd7046c5) + (a << 3);
   a = (a ^ 0xb55a4f09) ^ (a >> 16);
   return a;
}

// Simple hash color from uint value.
// from niagara stream. see https://www.youtube.com/watch?v=BR2my8OE1Sc
float3 simpleHashColor(uint i)
{
    uint h = simpleHash(i);
    return float3(float(h & 255), float((h >> 8) & 255), float((h >> 16) & 255)) / 255.0;
}

uint simpleHashColorPack(uint i)
{
    uint h = simpleHash(i);
    return shaderPackColor(h & 255, (h >> 8) & 255, (h >> 16) & 255, 255);
}

float2 screenUvToNdcUv(float2 uv)
{
    uv.x = 2.0 * (uv.x - 0.5);
    uv.y = 2.0 * (0.5 - uv.y);

    return uv;
}

float3 projectPosToUVz(float3 pos, in const float4x4 projectMatrix)
{
    float4 posHS = mul(projectMatrix, float4(pos, 1.0));
    posHS.xyz /= posHS.w; // xy is -1 to 1

    // NDC convert. 
    posHS.xy = 0.5 * posHS.xy + 0.5;
    posHS.y  = 1.0 - posHS.y;

    // Final project UVz.
    return posHS.xyz;
}

bool isUVzValid(float3 UVz)
{
    return all(UVz > 0.0) && all(UVz < 1.0);
}

static float3 kExtentApplyFactor[8] =
{
    float3( 1,  1,  1), // Extent first.
    float3(-1, -1, -1), // Extent first.
    float3( 1,  1, -1),
    float3( 1, -1,  1),
    float3(-1,  1,  1),
    float3( 1, -1, -1),
    float3(-1, -1,  1),
    float3(-1,  1, -1),
};

float4 projectScreen(float3 centerLS, float3 extentLS, float4x4 projectMatrix)
{
    float2 UVs[8];  

    [unroll(8)] 
    for (uint k = 0; k < 8; k ++)
    {
        const float3 extentPos = centerLS + extentLS * kExtentApplyFactor[k];
        UVs[k] = projectPosToUVz(extentPos, projectMatrix).xy;
    }

    float4 result = UVs[0].xyxy;
    [unroll(7)] 
    for(uint j = 1; j < 8; j ++)
    {
        result.xy = min(result.xy, UVs[j]);
        result.zw = max(result.zw, UVs[j]);
    }

    return result;
}

float4 transformSphere(float4 sphere, float4x4 nonPerspectiveProj, float maxScaleAbs) 
{
    float4 result;

    result.xyz = mul(nonPerspectiveProj, float4(sphere.xyz, 1.0)).xyz;
    result.w = maxScaleAbs * sphere.w;

    return result;
}

// Return true if culled. 
bool frustumCulling(float4 planesRS[6], float3 centerLS, float3 extentLS, float4x4 localToTranslatedWorld)
{
    // Find one plane which all extent point is back face of it.
    float3 extentPosRS[8];
    [unroll(8)] 
    for (uint k = 0; k < 8; k ++)
    {
        const float3 extentPos = centerLS + extentLS * kExtentApplyFactor[k];
        extentPosRS[k] = mul(localToTranslatedWorld, float4(extentPos, 1.0)).xyz;
    }

    [unroll(6)] 
    for (uint i = 0; i < 6; i ++)
    {
        const float4 plane = planesRS[i];
        bool bAllBackFace = true;

        for (uint j = 0; j < 8; j ++)
        {
            if (dot(plane.xyz, extentPosRS[j]) > -plane.w)
            {
                bAllBackFace = false;
                break;
            }
        }

        if (bAllBackFace) { return true; }
    }

    return false;
}

static uint4 kLODLevelDebugColor[8] =
{
    uint4(255, 0,   0,  255), // Extent first.
    uint4(125, 0, 255,  255), // Extent first.
    uint4( 39, 0, 255,  255), // Extent first.
    uint4(  0, 39, 255, 255), // Extent first.
    uint4(  0, 125, 39, 255), // Extent first.
    uint4(125, 125,  0, 255), // Extent first.
    uint4( 39, 255,  0, 255), // Extent first.
    uint4(  0, 255,  0, 255), // Extent first.
};

// Quad schedule style, fake pixel shader dispatch style.
// Input-> [0, 63]
//
// Output:
//  00 01 08 09 10 11 18 19
//  02 03 0a 0b 12 13 1a 1b
//  04 05 0c 0d 14 15 1c 1d
//  06 07 0e 0f 16 17 1e 1f
//  20 21 28 29 30 31 38 39
//  22 23 2a 2b 32 33 3a 3b
//  24 25 2c 2d 34 35 3c 3d
//  26 27 2e 2f 36 37 3e 3f
uint2 remap8x8(uint tid)
{
    return uint2((((tid >> 2) & 0x7) & 0xFFFE) | (tid & 0x1), ((tid >> 1) & 0x3) | (((tid >> 3) & 0x7) & 0xFFFC));
}

// Quad schedule style, fake pixel shader dispatch style.
// Input-> [0, 255]
//
// Output:
//  00 01 08 09 10 11 18 19 40 41 48 49 50 51 58 59
//  02 03 0a 0b 12 13 1a 1b 42 43 4a 4b 52 53 5a 5b
//  04 05 0c 0d 14 15 1c 1d 44 45 4c 4d 54 55 5c 5d
//  06 07 0e 0f 16 17 1e 1f 46 47 4e 4f 56 57 5e 5f
//  20 21 28 29 30 31 38 39 60 61 68 69 70 71 78 79
//  22 23 2a 2b 32 33 3a 3b 62 63 6a 6b 72 73 7a 7b
//  24 25 2c 2d 34 35 3c 3d 64 65 6c 6d 74 75 7c 7d
//  26 27 2e 2f 36 37 3e 3f 66 67 6e 6f 76 77 7e 7f
//  80 81 88 89 90 91 98 99 c0 c1 c8 c9 d0 d1 d8 d9
//  82 83 8a 8b 92 93 9a 9b c2 c3 ca cb d2 d3 da db
//  84 85 8c 8d 94 95 9c 9d c4 c5 cc cd d4 d5 dc dd
//  86 87 8e 8f 96 97 9e 9f c6 c7 ce cf d6 d7 de df
//  a0 a1 a8 a9 b0 b1 b8 b9 e0 e1 e8 e9 f0 f1 f8 f9
//  a2 a3 aa ab b2 b3 ba bb e2 e3 ea eb f2 f3 fa fb
//  a4 a5 ac ad b4 b5 bc bd e4 e5 ec ed f4 f5 fc fd
//  a6 a7 ae af b6 b7 be bf e6 e7 ee ef f6 f7 fe ff
uint2 remap16x16(uint tid)
{
    uint2 xy;
    uint2 basic = remap8x8(tid % 64);

    // offset x:   0-63  is 0
    //           64-127  is 8
    //           128-191 is 0
    //           192-255 is 8
    xy.x = basic.x + 8 * ((tid >> 6) % 2);

    // offset y:   0-63  is 0
    //           64-127  is 0
    //           128-191 is 8
    //           192-255 is 8
    xy.y = basic.y + 8 * ((tid >> 7));
    return xy;
}

float min4(float4 v) { return min(min(min(v.x, v.y), v.z), v.w); }
float max4(float4 v) { return max(max(max(v.x, v.y), v.z), v.w); }

float max2(float2 v) { return max(v.x, v.y); }

uint2 convert2d(uint id, uint dim)
{
    return uint2(id % 2, id / 2);
}

uint encodeShadingMeshlet(uint shadingType, uint meshletId)
{
    return ((meshletId & kMaxMeshletCount) << 7) | (shadingType & kMaxShadingType);
}

void decodeShadingMeshlet(uint pack, out uint shadingType, out uint meshletId)
{
    shadingType = (pack & kMaxShadingType);
    meshletId = (pack >> 7) & kMaxMeshletCount;
}

uint encodeTriangleIdObjectId(uint triangleId, uint objectId)
{
    return ((objectId & kMaxObjectCount) << 8) | (triangleId & 0xFF);
}

void decodeTriangleIdObjectId(uint pack, out uint triangleId, out uint objectId)
{
    triangleId = (pack & 0xFF);
    objectId   = (pack >> 8) & kMaxObjectCount;
}

uint4 getShadingType4(uint4 pack4)
{
    return pack4 & kMaxShadingType;
}

uint getShadingType(uint pack)
{
    return pack & kMaxShadingType;
}

struct Barycentrics
{
    float3 interpolation;
    float3 ddx;
    float3 ddy;
};

// Unreal Engine 5 Nanite improved perspective correct barycentric coordinates and partial derivatives using screen derivatives.
Barycentrics calculateTriangleBarycentrics(float2 PixelClip, float4 PointClip0, float4 PointClip1, float4 PointClip2, float2 ViewInvSize)
{
	Barycentrics barycentrics;

	const float3 RcpW = rcp(float3(PointClip0.w, PointClip1.w, PointClip2.w));
	const float3 Pos0 = PointClip0.xyz * RcpW.x;
	const float3 Pos1 = PointClip1.xyz * RcpW.y;
	const float3 Pos2 = PointClip2.xyz * RcpW.z;

	const float3 Pos120X = float3(Pos1.x, Pos2.x, Pos0.x);
	const float3 Pos120Y = float3(Pos1.y, Pos2.y, Pos0.y);
	const float3 Pos201X = float3(Pos2.x, Pos0.x, Pos1.x);
	const float3 Pos201Y = float3(Pos2.y, Pos0.y, Pos1.y);

	const float3 C_dx = Pos201Y - Pos120Y;
	const float3 C_dy = Pos120X - Pos201X;

	const float3 C = C_dx * (PixelClip.x - Pos120X) + C_dy * (PixelClip.y - Pos120Y);	// Evaluate the 3 edge functions
	const float3 G = C * RcpW;

	const float H = dot(C, RcpW);
	const float RcpH = rcp(H);

	// UVW = C * RcpW / dot(C, RcpW)
	barycentrics.interpolation = G * RcpH;

	// Texture coordinate derivatives:
	// UVW = G / H where G = C * RcpW and H = dot(C, RcpW)
	// UVW' = (G' * H - G * H') / H^2
	// float2 TexCoordDX = UVW_dx.y * TexCoord10 + UVW_dx.z * TexCoord20;
	// float2 TexCoordDY = UVW_dy.y * TexCoord10 + UVW_dy.z * TexCoord20;
	const float3 G_dx = C_dx * RcpW;
	const float3 G_dy = C_dy * RcpW;

	const float H_dx = dot(C_dx, RcpW);
	const float H_dy = dot(C_dy, RcpW);

	barycentrics.ddx = (G_dx * H - G * H_dx) * (RcpH * RcpH) * ( 2.0f * ViewInvSize.x);
	barycentrics.ddy = (G_dy * H - G * H_dy) * (RcpH * RcpH) * (-2.0f * ViewInvSize.y);

	return barycentrics;
}

// https://stackoverflow.com/questions/21648630/radius-of-projected-sphere-in-screen-space
// return radius of pixel of current sphere project result.
// return negative if eye in the sphere.
float projectSphereToScreen(float4 sphere, float height, float fovy)
{
    float halfFovy = 0.5 * fovy;

    //
    const float d2 = dot(sphere.xyz, sphere.xyz);
    const float r  = sphere.w;
    const float r2 = r * r;
    if (d2 <= r2)
    {
        return -1.0f; // eye in sphere.
    }

    //
    return height * 0.5 / tan(halfFovy) * r / sqrt(d2 - r2);
}

#endif // !SHADER_BASE_HLSLI