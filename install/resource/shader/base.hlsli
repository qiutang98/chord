#ifndef SHADER_BASE_HLSLI
#define SHADER_BASE_HLSLI

#include "base.h"

// Ray tracing flags
/**
enum RAY_FLAG : uint
{
    RAY_FLAG_NONE = 0x00,
    RAY_FLAG_FORCE_OPAQUE = 0x01,
    RAY_FLAG_FORCE_NON_OPAQUE = 0x02,
    RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH = 0x04,
    RAY_FLAG_SKIP_CLOSEST_HIT_SHADER = 0x08,
    RAY_FLAG_CULL_BACK_FACING_TRIANGLES = 0x10,
    RAY_FLAG_CULL_FRONT_FACING_TRIANGLES = 0x20,
    RAY_FLAG_CULL_OPAQUE = 0x40,
    RAY_FLAG_CULL_NON_OPAQUE = 0x80,
    RAY_FLAG_SKIP_TRIANGLES = 0x100,
    RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES = 0x200,
};
**/

// 
#define GIRayQuery RayQuery<RAY_FLAG_CULL_NON_OPAQUE>

#define kPI 3.14159265358979323846
#define kInvertPI (1.0 / kPI)
#define KPIOver2  (kPI / 2.0)
#define KPIOver4  (kPI / 4.0)
#define kNaN asfloat(0x7FC00000)

#define kFloatEpsilon 1.192092896e-07f

// Infinite invert z can't just use zero, it will produce nan, we use epsilon.
#define kFarPlaneZ kFloatEpsilon

float1 max0(float1 v) { return max(v, 0.0); }
float2 max0(float2 v) { return max(v, 0.0); }
float3 max0(float3 v) { return max(v, 0.0); }
float4 max0(float4 v) { return max(v, 0.0); }

float1 mod(float1 x, float1 y) { return x - y * floor(x / y); }
float2 mod(float2 x, float2 y) { return x - y * floor(x / y); }
float3 mod(float3 x, float3 y) { return x - y * floor(x / y); }
float4 mod(float4 x, float4 y) { return x - y * floor(x / y); }

float1 log10(float1 x) { return log(x) / log(10.0); }
float2 log10(float2 x) { return log(x) / log(10.0); }
float3 log10(float3 x) { return log(x) / log(10.0); }
float4 log10(float4 x) { return log(x) / log(10.0); }

float1 linstep(float1 min, float1 max, float1 v) { return clamp((v - min) / (max - min), 0, 1); }
float2 linstep(float2 min, float2 max, float2 v) { return clamp((v - min) / (max - min), 0, 1); }
float3 linstep(float3 min, float3 max, float3 v) { return clamp((v - min) / (max - min), 0, 1); }
float4 linstep(float4 min, float4 max, float4 v) { return clamp((v - min) / (max - min), 0, 1); }

float max3(float x, float y, float z) { return max(x, max(y, z)); }
float min3(float x, float y, float z) { return min(x, min(y, z)); }

// https://gpuopen.com/learn/optimized-reversible-tonemapper-for-resolve/
// https://www.shadertoy.com/view/Xdd3Rr
float3 fastTonemap(float3 c) { return c * rcp(max3(c.r, c.g, c.b) + 1.0); }
float3 fastTonemapInvert(float3 c) { return c * rcp(1.0 - max3(c.r, c.g, c.b)); }

// Default sign(x) return zero when x equal to zero. 
float1 signNotZero(float1 v) { return select(v >= 0.f, 1.f, -1.f); }
float2 signNotZero(float2 v) { return select(v >= 0.f, 1.f, -1.f); }
float3 signNotZero(float3 v) { return select(v >= 0.f, 1.f, -1.f); }
float4 signNotZero(float4 v) { return select(v >= 0.f, 1.f, -1.f); }

float3 getDiffuseColor(float3 baseColor, float metallic, float3 f0 = 0.04)
{
    return baseColor * (1.0 - f0) * (1.0 - metallic);
}

float3 getSpecularColor(float3 baseColor, float metallic, float3 f0 = 0.04)
{
    return lerp(f0, baseColor, metallic);
}

// Lambert lighting
// see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
float3 Fd_LambertDiffuse(float3 diffuseColor)
{
    return diffuseColor / kPI;
}

float maxComponent(float3 a)
{
    return max(a.x, max(a.y, a.z));
}


/**
    Gather pattern

    https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/gather4--sm5---asm-

    This instruction behaves like the sample instruction, but a filtered sample is not generated. 
    The four samples that would contribute to filtering are placed into xyzw in counter clockwise order 
    starting with the sample to the lower left of the queried location. 
    
    This is the same as point sampling with (u,v) texture coordinate deltas at the following locations: 
    (-,+),(+,+),(+,-),(-,-), where the magnitude of the deltas are always half a texel.

**/
static const int2 kGatherOffset[4] = { int2(-1, +1), int2(+1, +1), int2(+1, -1), int2(-1, -1) }; 

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

float3 getPositionRS(float2 uv, float z, in PerframeCameraView view)
{
    const float4 posCS   = float4(screenUvToNdcUv(uv), z, 1.0);
    const float4 posRS_H = mul(view.clipToTranslatedWorld, posCS);

    return posRS_H.xyz / posRS_H.w;
}

float3 getPositionRS_LastFrame(float2 uv, float z, in PerframeCameraView view)
{
    const float4 posCS   = float4(screenUvToNdcUv(uv), z, 1.0);
    const float4 posRS_H = mul(view.clipToTranslatedWorld_LastFrame, posCS);

    return posRS_H.xyz / posRS_H.w;
}

float3 projectPosToUVz(float3 pos, in const float4x4 projectMatrix)
{
    float4 posHS = mul(projectMatrix, float4(pos, 1.0));
    posHS.xyz /= posHS.w; // xy is -1 to 1

    // NDC convert. 
    posHS.xy =  posHS.xy * float2(0.5, -0.5) + 0.5;

    // Final project UVz.
    return posHS.xyz;
}

float4 quantize(float4 x, float count) { return ceil(x * count - 1.0f) / (count - 1.0f); }
float3 quantize(float3 x, float count) { return ceil(x * count - 1.0f) / (count - 1.0f); }
float2 quantize(float2 x, float count) { return ceil(x * count - 1.0f) / (count - 1.0f); }
float1 quantize(float1 x, float count) { return ceil(x * count - 1.0f) / (count - 1.0f); }


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

static int2 kBilinearOffset[4] = 
{
    int2(0, 0),
    int2(1, 0),
    int2(0, 1),
    int2(1, 1),
};

float4 getBilinearWeight(float2 xy) // xy -> [0, 1]
{
    float x = xy.x;
    float y = xy.y;
    return float4((1.0 - x) * (1.0 - y), x * (1.0 - y), (1.0 - x) * y, x * y);
}

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

const bool isOrthoProjection(float4x4 projectMatrix)
{
    return projectMatrix[3][3] == 1.0f;
}


// Project to ndc do frustum culling.
// Return true if culled.
bool orthoFrustumCulling(float3 centerLS, float3 extentLS, float4x4 localToClip)
{
    float3 extentUVz[8];
    [unroll(8)] 
    for (uint k = 0; k < 8; k ++)
    {
        const float3 extentPos = centerLS + extentLS * kExtentApplyFactor[k];
        extentUVz[k] = projectPosToUVz(extentPos, localToClip);
    }

    float3 minUVz =  10.0f;
    float3 maxUVz = -10.0f;
    [unroll(8)] 
    for(uint j = 0; j < 8; j ++)
    {
        minUVz = min(minUVz, extentUVz[j]);
        maxUVz = max(maxUVz, extentUVz[j]);
    }

    // Only do non cross near or far plane culling.
    return any(minUVz.xy >= 1) || any(maxUVz.xy <= 0); 
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

// Return true if culled. 
bool frustumCulling(float4 planesRS[6], float3 centerRS, float3 extentRS)
{
    // Find one plane which all extent point is back face of it.
    float3 extentPosRS[8];
    [unroll(8)] 
    for (uint k = 0; k < 8; k ++)
    {
        extentPosRS[k] = centerRS + extentRS * kExtentApplyFactor[k];
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

float min4(float4 v) 
{ 
    return min(min(min(v.x, v.y), v.z), v.w); 
}

float max4(float4 v) 
{
    return max(max(max(v.x, v.y), v.z), v.w); 
}

half min4(half4 v) 
{ 
    return min(min(min(v.x, v.y), v.z), v.w); 
}

half max4(half4 v) 
{ 
    return max(max(max(v.x, v.y), v.z), v.w); 
}

float max2(float2 v) 
{ 
    return max(v.x, v.y); 
}

uint2 convert2d(uint id, uint dim)
{
    return uint2(id % 2, id / 2);
}

uint encodeTriangleIdInstanceId(uint triangleId, uint instanceId)
{
    uint nextInstanceId = instanceId + 1;
    return ((nextInstanceId & kMaxInstanceIdCount) << 8) | (triangleId & 0xFF);
}

void decodeTriangleIdInstanceId(uint pack, out uint triangleId, out uint instanceId)
{
    triangleId = (pack & 0xFF);
    instanceId = ((pack >> 8) & kMaxInstanceIdCount) - 1;
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

// R2 based jitter sequence.
uint2 jitterSequence(uint index, uint2 dimension, uint2 dispatchId)
{
    // 0.7548776662467, 0.569840290998
    // 0.2451223337530, 0.430159709002
	uint2 offset = uint2(float2(0.7548776662467, 0.569840290998) * index * dimension);
    uint2 offsetId = dispatchId + offset;

    offsetId.x = offsetId.x % dimension.x;
    offsetId.y = offsetId.y % dimension.y;

	return offsetId;
}

uint2 jitterSequence2(uint index, uint2 dimension, uint2 dispatchId)
{
    // 0.7548776662467, 0.569840290998
    // 0.2451223337530, 0.430159709002
	uint2 offset = uint2(float2(0.2451223337530, 0.430159709002) * index * dimension);
    uint2 offsetId = dispatchId + offset;

    offsetId.x = offsetId.x % dimension.x;
    offsetId.y = offsetId.y % dimension.y;

	return offsetId;
}

// https://www.shadertoy.com/view/4lscWj
// Radical inverse based on http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float2 hammersley2d(uint i, uint N) 
{
    // Efficient VanDerCorpus calculation.
	uint bits = (i << 16u) | (i >> 16u);
         bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
         bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
         bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
         bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    //
	float rdi = float(bits) * 2.3283064365386963e-10;

    // Hammersley sequence.
	return float2(float(i) / float(N), rdi);
}

// high frequency dither pattern appearing almost random without banding steps
// note: from "NEXT GENERATION POST PROCESSING IN CALL OF DUTY: ADVANCED WARFARE"
//      http://advances.realtimerendering.com/s2014/index.html
float interleavedGradientNoise(float2 uv, float frameId)
{
	// magic values are found by experimentation
	uv += frameId * (float2(47.0, 17.0) * 0.695f);
    const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(uv, magic.xy)));
}

// Uchimura 2017, "HDR theory and practice"
// Math: https://www.desmos.com/calculator/gslcdxvipg
// Source: https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
float uchimuraTonemapperComponent(float x, float P, float a, float m, float l0, float c, float b, float S0, float S1, float CP) 
{
    float w0 = 1.0 - smoothstep(0.0, m, x);
    float w2 = step(m + l0, x);
    float w1 = 1.0 - w0 - w2;
    float T  = m * pow(x / m, c) + b;
    float S  = P - (P - S1) * exp(CP * (x - S0));
    float L  = m + a * (x - m);

    return T * w0 + L * w1 + S * w2;
}

float3 uchimuraTonemapper(float3 color, float P, float a, float m, float l0, float c, float b, float S0, float S1, float CP)
{
    float3 result;
    result.x = uchimuraTonemapperComponent(color.x, P, a, m, l0, c, b, S0, S1, CP);
    result.y = uchimuraTonemapperComponent(color.y, P, a, m, l0, c, b, S0, S1, CP);
    result.z = uchimuraTonemapperComponent(color.z, P, a, m, l0, c, b, S0, S1, CP);
    return result;
}

// https://www.shadertoy.com/view/Mtc3Ds
// rayleigh phase function.
float rayleighPhase(float a) 
{
    const float k = 3.0 / (16.0 * kPI);
    return k * (1.0 + a * a);
}

// Cornette-Shanks (CS) [1992]: Physically reasonable analytic expression for the single-scattering phase function
// Schlick approximation mie phase function.
float cornetteShanksMiePhase(float g, float VoL)
{
    float g2 = g * g;

	float k = 3.0 / (8.0 * kPI) * (1.0 - g2) / (2.0 + g2);
	return k * (1.0 + VoL * VoL) / pow(1.0 + g2 - 2.0 * g * VoL, 1.5);
}

// Henyey-Greenstein (HG) [1941] http://www.pbr-book.org/3ed-2018/Volume_Scattering/Phase_Functions.html
float henyeyGreensteinPhase(float g, float VoL)
{
    float a = -VoL; // Assume inp
    float g2 = g * g;

	float denom = 1.0 + g2 + 2.0 * g * a;
	return (1.0 - g2) / (4.0f * kPI * denom * sqrt(denom));
}

// NOTE: henyeyGreensteinPhase looks same with cornetteShanksMiePhase in most case and henyeyGreensteinPhase compute faster.

// Vulkan linearize z.
// NOTE: viewspace z range is [-zFar, -zNear], linear z is viewspace z mul -1 result on vulkan.
// if no exist reverse z:
//       linearZ = zNear * zFar / (zFar +  deviceZ * (zNear - zFar));
//  when reverse z enable, then the function is:
//       linearZ = zNear * zFar / (zNear + deviceZ * (zFar - zNear));

// Right hand look at matrix.
float4x4 lookAt_RH(float3 eye, float3 center, float3 up)
{
    const float3 f = normalize(center - eye);
    const float3 s = normalize(cross(f, up));
    const float3 u = cross(s, f);

    float4x4 ret = 0;

    ret[0][0] = s.x; 
    ret[0][1] = u.x; 
    ret[0][2] =-f.x; 
    ret[0][3] = 0.0; 

    ret[1][0] = s.y; 
    ret[1][1] = u.y;
    ret[1][2] =-f.y;
    ret[1][3] = 0.0;

    ret[2][0] = s.z; 
    ret[2][1] = u.z; 
    ret[2][2] =-f.z; 
    ret[2][3] = 0.0;

    ret[3][0] = -dot(s, eye);
    ret[3][1] = -dot(u, eye);
    ret[3][2] =  dot(f, eye);
    ret[3][3] = 1.0;

    return transpose(ret);
}

float4x4 ortho_RH_ZeroOne(float left, float right, float bottom, float top, float zNear, float zFar)
{
    float4x4 ret = 0;

    ret[0][0] =   2.0f / (right - left);
    ret[1][1] =   2.0f / (top - bottom);
    ret[2][2] =  -1.0f / (zFar - zNear);
    ret[3][0] = -(right + left) / (right - left);
    ret[3][1] = -(top + bottom) / (top - bottom);
    ret[3][2] = -zNear / (zFar - zNear);
    ret[3][3] =  1.0;

    
    return transpose(ret);
}

float4x4 matrixInverse(float4x4 m)
{
	float n11 = m[0][0], n12 = m[1][0], n13 = m[2][0], n14 = m[3][0];
	float n21 = m[0][1], n22 = m[1][1], n23 = m[2][1], n24 = m[3][1];
	float n31 = m[0][2], n32 = m[1][2], n33 = m[2][2], n34 = m[3][2];
	float n41 = m[0][3], n42 = m[1][3], n43 = m[2][3], n44 = m[3][3];

	float t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
	float t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
	float t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
	float t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;

	float det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
	float idet = 1.0f / det;

	float4x4 ret;

	ret[0][0] = t11 * idet;
	ret[0][1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * idet;
	ret[0][2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * idet;
	ret[0][3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * idet;

	ret[1][0] = t12 * idet;
	ret[1][1] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * idet;
	ret[1][2] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * idet;
	ret[1][3] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * idet;

	ret[2][0] = t13 * idet;
	ret[2][1] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * idet;
	ret[2][2] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * idet;
	ret[2][3] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * idet;

	ret[3][0] = t14 * idet;
	ret[3][1] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * idet;
	ret[3][2] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * idet;
	ret[3][3] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * idet;

	return ret;
}

// 3x3 Sample pattern
// https://qiutang98.github.io/post/%E5%AE%9E%E6%97%B6%E6%B8%B2%E6%9F%93%E5%BC%80%E5%8F%91/wave_accelerate_3x3/
static const int2 k3x3QuadSampleSigned[4] = 
{
    int2(-1, -1),
    int2(+1, -1),
    int2(-1, +1),
    int2(+1, +1),
};

static const int2 k3x3QuadSampleOffset[4] = 
{
    int2(0, 0), // Central
    int2(1, 1),
    int2(0, 1),
    int2(1, 0), 
};
// Example usage:
/*
    uint quadIndex = WaveGetLaneIndex() % 4;
    float shadowMask[9];
    [unroll(4)]
    for (int i = 0; i < 4; i ++)
    {
        int2 samplePos = workPos + k3x3QuadSampleOffset[i] * k3x3QuadSampleSigned[quadIndex];
        samplePos = clamp(samplePos, 0, pushConsts.dim - 1);

        shadowMask[i] = shadowMaskTexture[samplePos];
    }

    shadowMask[4] =        QuadReadAcrossX(shadowMask[0]);
    shadowMask[5] =        QuadReadAcrossY(shadowMask[0]);
    shadowMask[6] = QuadReadAcrossDiagonal(shadowMask[0]);
    shadowMask[7] = QuadReadAcrossX(shadowMask[2]);
    shadowMask[8] = QuadReadAcrossY(shadowMask[3]);

    float shadowMaskSum = 0.0;
    [unroll(9)]
    for (int i = 0; i < 9; i ++)
    {
        shadowMaskSum += shadowMask[i];
    }
*/



// 
float chebyshevUpperBound(float2 moments, float mean, float minVariance)
{
    // Standard shadow map comparison
    float p = (mean >= moments.x);
    
    // Compute variance
    float variance = moments.y - (moments.x * moments.x);
    variance = max(variance, minVariance);
    
    // Compute probabilistic upper bound
    float d     = mean - moments.x;
    float p_max = variance / (variance + d * d);
    
    return max(p, p_max);
}

// https://www.pcg-random.org/

uint pcgHash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint pcgHash(uint2 values)
{
    values = values * 1664525u + 1013904223u;

    values.x += values.y * 1664525u;
    values.y += values.x * 1664525u;

    values = values ^ (values >> 16u);

    values.x += values.y * 1664525u;
    values.y += values.x * 1664525u;

    values = values ^ (values >> 16u);

    return dot(values, 1);
}

uint pcgHash(uint3 values)
{
    values = values * 1664525u + 1013904223u;

    values.x += values.y * values.z;
    values.y += values.z * values.x;
    values.z += values.x * values.y;

    values ^= values >> 16u;

    values.x += values.y * values.z;
    values.y += values.z * values.x;
    values.z += values.x * values.y;

    return dot(values, 1);
}

uint pcgHash(uint4 values)
{
    values = values * 1664525u + 1013904223u;

    values.x += values.y * values.w;
    values.y += values.z * values.x;
    values.z += values.x * values.y;
    values.w += values.y * values.z;

    values ^= values >> 16u;

    values.x += values.y * values.w;
    values.y += values.z * values.x;
    values.z += values.x * values.y;
    values.w += values.y * values.z;

    return dot(values, 1);
}

// xxhash (https://github.com/Cyan4973/xxHash)
uint xxHash(uint value)
{
    const uint prime32_2 = 2246822519u, prime32_3 = 3266489917u;
    const uint prime32_4 = 668265263u,  prime32_5 = 374761393u;

    uint ret = value + prime32_5;
         ret = prime32_4 * ((ret << 17) | (ret >> (32 - 17)));
         ret = prime32_2 * (ret ^ (ret >> 15));
         ret = prime32_3 * (ret ^ (ret >> 13));

    return ret ^ (ret >> 16);
}

uint xxHash(uint2 values)
{
    const uint prime32_2 = 2246822519u, prime32_3 = 3266489917u;
    const uint prime32_4 = 668265263u,  prime32_5 = 374761393u;

    uint ret = values.y + prime32_5 + values.x * prime32_3;
         ret = prime32_4 * ((ret << 17) | (ret >> (32 - 17)));
         ret = prime32_2 * (ret ^ (ret >> 15));
         ret = prime32_3 * (ret ^ (ret >> 13));

    return ret ^ (ret >> 16);
}

uint xxHash(uint3 values)
{
    const uint prime32_2 = 2246822519u, prime32_3 = 3266489917u;
    const uint prime32_4 = 668265263u,  prime32_5 = 374761393u;

    uint ret  = values.z + prime32_5 + values.x * prime32_3;
         ret  = prime32_4 * ((ret << 17) | (ret >> (32 - 17)));
         ret += values.y * prime32_3;
         ret  = prime32_4 * ((ret << 17) | (ret >> (32 - 17)));
         ret  = prime32_2 * (ret ^ (ret >> 15));
         ret  = prime32_3 * (ret ^ (ret >> 13));

    return ret ^ (ret >> 16);
}

uint xxHash(uint4 values)
{
    const uint prime32_2 = 2246822519u, prime32_3 = 3266489917u;
    const uint prime32_4 = 668265263u,  prime32_5 = 374761393u;

    uint ret  = values.w + prime32_5 + values.x * prime32_3;
         ret  = prime32_4 * ((ret << 17) | (ret >> (32 - 17)));
         ret += values.y * prime32_3;
         ret  = prime32_4 * ((ret << 17) | (ret >> (32 - 17)));
         ret += values.z * prime32_3;
         ret  = prime32_4 * ((ret << 17) | (ret >> (32 - 17)));
         ret  = prime32_2 * (ret ^ (ret >> 15));
         ret  = prime32_3 * (ret ^ (ret >> 13));

    return ret ^ (ret >> 16);
}

float trigHash(float2 value)
{
    return frac(43757.5453f * sin(dot(value, float2(12.9898f, 78.233f))));
}

float trigHash(float3 values)
{
    return trigHash(float2(trigHash(values.xy), values.z));
}

float trigHash(float4 values)
{
    return trigHash(float3(trigHash(values.xy), values.z, values.w));
}

RayDesc getRayDesc(float3 o, float3 d, float tMin = kDefaultRayQueryTMin, float tMax = kDefaultRayQueryTMax)
{
    RayDesc ray;

    // check(tMin < tMax)

    // 
	ray.TMin = tMin;
	ray.TMax = tMax;

    //
	ray.Origin    = o;
	ray.Direction = d;

    return ray;
}


// Octahedron Normal Vectors
// [Cigolle 2014, "A Survey of Efficient Representations for Independent Unit Vectors"]

// result uv in [-1, +1]
float2 octahedralEncode(float3 N)
{
	N.xy /= dot(1, abs(N));
	if (N.z <= 0)
	{
		N.xy = (1 - abs(N.yx)) * select(N.xy >= 0, float2(1, 1), float2(-1, -1));
	}
	return N.xy;
}

// 
float3 octahedralDecode(float2 Oct)
{
	float3 N = float3( Oct, 1 - dot( 1, abs(Oct) ) );
	float t = max( -N.z, 0 );
	N.xy += select(N.xy >= 0, float2(-t, -t), float2(t, t));
	return normalize(N);
}

// Z up hemisphere octahedral encode.
float2 hemiOctahedralEncode(float3 direction)
{
	direction.xy /= dot(1, abs(direction));
	return float2(direction.x + direction.y, direction.x - direction.y); // -1, +1
}

// Z up hemisphere octahedral decoded.
float3 hemiOctahedralDecode(float2 coords)
{
	coords = float2(coords.x + coords.y, coords.x - coords.y);
	float3 direction = float3(coords, 2.0 - dot(1, abs(coords)));
	return normalize(direction);
}

uint pack_dir_2_uint(float3 dir)
{
    float2 uv = octahedralEncode(dir) * 0.5 + 0.5; // [0, 1]

    uint2 p = f32tof16(uv);
    return p.x | (p.y << 16u);
}

float3 unpack_dir_f_uint(uint p)
{
    uint2 p2;

    p2.x = (p >> 0u) & 0xffffu;
    p2.y = (p >> 16u) & 0xffffu;

    float2 uv = f16tof32(p2);
    return octahedralDecode(uv * 2.0 - 1.0);
}

// Tangent vector t: t is z up. 
// convert = t.x * tbn[0] + t.y * tbn[1] + t.z * tbn[2]
// invert  = float3(dot(w, tbn[0]), dot(w, tbn[1]), dot(w, tbn[2]))
float3x3 createTBN(float3 n)
{
    float3 u;
    if (abs(n.z) > 0.0)
    {
        float k = sqrt(dot(n.yz, n.yz));
        u.x =  0.0;
        u.y = -n.z / k;
        u.z =  n.y / k; 
    }
    else
    {
        float k = sqrt(dot(n.xy, n.xy));
        u.x =  n.y / k;
        u.y = -n.x / k;
        u.z =  0.0;
    }

    float3x3 tbn;
    tbn[0] = u;
    tbn[1] = cross(n, u);
    tbn[2] = n;

    return tbn;
}

float3 tbnTransform(float3x3 tbn, float3 dir)
{
    return dir.x * tbn[0] + dir.y * tbn[1] + dir.z * tbn[2];
}

float3 tbnInverseTransform(float3x3 tbn, float3 dir)
{
    return float3(dot(dir, tbn[0]), dot(dir, tbn[1]), dot(dir, tbn[2]));
}

uint pack_float2_t_uint(float2 v)
{
    uint2 v_h = f32tof16(v);
    return v_h.x | (v_h.y << 16u);
}

float2 unpack_float2_f_uint(uint r)
{
    uint2 v_h;

    v_h.x = (r >>  0u) & 0xffffu;
    v_h.y = (r >> 16u) & 0xffffu;

    return f16tof32(v_h);
}

uint2 pack_float4_t_uint2(float4 v)
{
    uint4 v_h = f32tof16(v);

    uint2 r;

    r.x = v_h.x | (v_h.y << 16u);
    r.y = v_h.z | (v_h.w << 16u);
    
    return r;
}

float4 unpack_float4_f_uint2(uint2 r)
{
    uint4 v_h;

    v_h.x = (r.x >>  0u) & 0xffffu;
    v_h.y = (r.x >> 16u) & 0xffffu;
    v_h.z = (r.y >>  0u) & 0xffffu;
    v_h.w = (r.y >> 16u) & 0xffffu;

    return f16tof32(v_h);
}

uint zOrder3DEncode(uint3 coord, const uint dimension)
{
    uint index = 0;
    const uint stepCount = log2(dimension);
    
    for (uint i = 0; i < stepCount; i++)
    {
        index |= ((coord.x >> i) & 0x1) << (3 * i + 0);
        index |= ((coord.y >> i) & 0x1) << (3 * i + 1);
        index |= ((coord.z >> i) & 0x1) << (3 * i + 2);
    }

    return index;
}

// This function take a rgb color (best is to provide color in sRGB space)
// and return a YCoCg color in [0..1] space for 8bit (An offset is apply in the function)
// Ref: http://www.nvidia.com/object/real-time-ycocg-dxt-compression.html
#define YCOCG_CHROMA_BIAS (128.0 / 255.0)
float3 RGBToYCoCg_srgb(float3 rgb)
{
    float3 YCoCg;
    YCoCg.x = dot(rgb, float3(0.25, 0.5, 0.25));
    YCoCg.y = dot(rgb, float3(0.5, 0.0, -0.5))    + YCOCG_CHROMA_BIAS;
    YCoCg.z = dot(rgb, float3(-0.25, 0.5, -0.25)) + YCOCG_CHROMA_BIAS;

    return YCoCg;
}

float3 YCoCgToRGB_srgb(float3 YCoCg)
{
    float Y = YCoCg.x;

    // 
    float Co = YCoCg.y - YCOCG_CHROMA_BIAS;
    float Cg = YCoCg.z - YCOCG_CHROMA_BIAS;

    float3 rgb;
    rgb.r = Y + Co - Cg;
    rgb.g = Y + Cg;
    rgb.b = Y - Co - Cg;

    return rgb;
}

// https://github.com/playdeadgames/temporal
float3 clipAABB_compute(float3 aabbMin, float3 aabbMax, float3 testSample, float bias)
{
    float3 aabbCenter = 0.5 * (aabbMax + aabbMin);
    float3 extentClip = 0.5 * (aabbMax - aabbMin) + bias;


    float3 colorVector = testSample - aabbCenter;
    float3 colorVectorClip = colorVector / extentClip;

    colorVectorClip  = abs(colorVectorClip);
    float maxAbsUnit = max(max(colorVectorClip.x, colorVectorClip.y), colorVectorClip.z);

    if (maxAbsUnit > 1.0) 
    {
        return aabbCenter + colorVector / maxAbsUnit; 
    } 

    // point is inside aabb
    return testSample; 
}
 
// From Open Asset Import Library
// https://github.com/assimp/assimp/blob/master/include/assimp/matrix3x3.inl
float3x3 rotFromToMatrix(float3 from, float3 to)
{
    float e = dot(from, to);
    float f = abs(e);

    if (f > 1.0f - 0.0003f)
    {
        return float3x3(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
    }

    float3 v   = cross(from, to);
    float h    = 1.f / (1.f + e);      /* optimization by Gottfried Chen */
    float hvx  = h * v.x;
    float hvz  = h * v.z;
    float hvxy = hvx * v.y;
    float hvxz = hvx * v.z;
    float hvyz = hvz * v.y;

    float3x3 mtx;
    mtx[0][0] = e + hvx * v.x;
    mtx[0][1] = hvxy - v.z;
    mtx[0][2] = hvxz + v.y;

    mtx[1][0] = hvxy + v.z;
    mtx[1][1] = e + h * v.y * v.y;
    mtx[1][2] = hvyz - v.x;

    mtx[2][0] = hvxz - v.y;
    mtx[2][1] = hvyz + v.x;
    mtx[2][2] = e + hvz * v.z;

    return mtx;
}



// max absolute error 9.0x10^-3
// Eberly's polynomial degree 1 - respect bounds
// 4 VGPR, 12 FR (8 FR, 1 QR), 1 scalar
// input [-1, 1] and output [0, PI]
float acosFast(float inX) 
{
    float x = abs(inX);
    float res = -0.156583f * x + (0.5 * kPI);
    res *= sqrt(1.0f - x);
    return (inX >= 0) ? res : kPI - res;
}

float acosFastPositive(float x)
{
    float p = -0.1565827 * x + 1.570796;
    return p * sqrt(1.0 - x);
}

#endif // !SHADER_BASE_HLSLI