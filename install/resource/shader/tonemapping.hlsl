#include "colorspace.h"

struct TonemappingPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(TonemappingPushConsts);

    uint textureId;
    uint pointClampSamplerId;
};
CHORD_PUSHCONST(TonemappingPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli" 
#include "fullscreen.hlsl"

#include "aces.hlsli"

static const float3x3 sRGB_2_AP1 = mul(XYZ_2_AP1_MAT,  mul(D65_2_D60_CAT, sRGB_2_XYZ_MAT));
static const float3x3 AP1_2_sRGB = mul(XYZ_2_sRGB_MAT, mul(D60_2_D65_CAT,  AP1_2_XYZ_MAT));
static const float3x3 AP0_2_AP1  = mul(XYZ_2_AP1_MAT, AP0_2_XYZ_MAT);
static const float3x3 AP1_2_AP0  = mul(XYZ_2_AP0_MAT, AP1_2_XYZ_MAT);

// Customize film tonemapper, See RRT in aces.hlsli.
float3 filmToneMap(
	float3 colorAP1, 
	float filmSlope, 
	float filmToe, 
	float filmShoulder, 
	float filmBlackClip, 
	float filmWhiteClip,
	float filmPreDesaturate,
	float filmPostDesaturate,
	float filmRedModifier,
	float filmGlowScale) 
{
    // Tonemapping in color space AP0
	float3 colorAP0 = mul(AP1_2_AP0, colorAP1);

	// "Glow" module constants, tons of magic numbers here.
    float saturation;
    {
        const float RRT_GLOW_GAIN = 0.05;
        const float RRT_GLOW_MID  = 0.08;

        saturation = rgb_2_saturation(colorAP0);
        float ycIn = rgb_2_yc(colorAP0, 1.75);
        float s = sigmoid_shaper((saturation - 0.4) / 0.2); // 
        float addedGlow = 1 + glow_fwd(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID) * filmGlowScale;

        // Apply glow.
        colorAP0 *= addedGlow;
    }

	// --- Red modifier --- //
    {
        const float RRT_RED_SCALE = 0.82;
        const float RRT_RED_PIVOT = 0.03;
        const float RRT_RED_HUE   = 0;
        const float RRT_RED_WIDTH = 135;

        float hue = rgb_2_hue(colorAP0);

        float centeredHue = center_hue(hue, RRT_RED_HUE);
        float hueWeight = smoothstep(0, 1, 1 - abs(2 * centeredHue / RRT_RED_WIDTH));
        hueWeight = hueWeight * hueWeight;

        // Weight based lerp.
        colorAP0.r += lerp(0.0, hueWeight * saturation * (RRT_RED_PIVOT - colorAP0.r) * (1.0 - RRT_RED_SCALE), filmRedModifier);
    }

	// Use ACEScg primaries as working space
	float3 workingColor = max0(mul(AP0_2_AP1, colorAP0));

	// Pre desaturate
	workingColor = lerp(dot(workingColor, AP1_RGB2Y), workingColor, filmPreDesaturate);

    // Unreal style film tonemapping.
    {
        const float toeScale = 1.0 + filmBlackClip - filmToe;
        const float shoulderScale = 1.0 + filmWhiteClip - filmShoulder;
        
        const float inMatch = 0.18;
        const float outMatch = 0.18;

        float toeMatch;
        if (filmToe > 0.8)
        {
            // 0.18 will be on straight segment
            toeMatch = (1 - filmToe  - outMatch) / filmSlope + log10(inMatch);
        }
        else
        {
            // 0.18 will be on toe segment

            // Solve for toeMatch such that input of inMatch gives output of outMatch.
            const float bt = (outMatch + filmBlackClip) / toeScale - 1;
            toeMatch = log10(inMatch) - 0.5 * log((1+bt) / (1-bt)) * (toeScale / filmSlope);
        }

        float straightMatch = (1.0 - filmToe) / filmSlope - toeMatch;
        float shoulderMatch = filmShoulder / filmSlope - straightMatch;
        
        float3 logColor = log10(workingColor);
        float3 straightColor = filmSlope * (logColor + straightMatch);
        
        float3 toeColor		    = (    - filmBlackClip ) + (2.0 *      toeScale) / (1.0 + exp( (-2.0 * filmSlope /      toeScale) * (logColor -      toeMatch)));
        float3 shoulderColor	= (1.0 + filmWhiteClip ) - (2.0 * shoulderScale) / (1.0 + exp( ( 2.0 * filmSlope / shoulderScale) * (logColor - shoulderMatch)));

        toeColor.x		= logColor.x <      toeMatch ?      toeColor.x : straightColor.x;
        toeColor.y		= logColor.y <      toeMatch ?      toeColor.y : straightColor.y;
        toeColor.z		= logColor.z <      toeMatch ?      toeColor.z : straightColor.z;

        shoulderColor.x	= logColor.x > shoulderMatch ? shoulderColor.x : straightColor.x;
        shoulderColor.y	= logColor.y > shoulderMatch ? shoulderColor.y : straightColor.y;
        shoulderColor.z	= logColor.z > shoulderMatch ? shoulderColor.z : straightColor.z;

        float3 t = saturate((logColor - toeMatch) / (shoulderMatch - toeMatch));
        t = shoulderMatch < toeMatch ? 1 - t : t;
        t = (3.0 - 2.0 * t) * t * t;

        workingColor = lerp(toeColor, shoulderColor, t);
    }

	// Post desaturate
	workingColor = lerp(dot(workingColor, AP1_RGB2Y), workingColor, filmPostDesaturate);

    // Return positive color in AP1.
	return max0(workingColor);
}


void mainPS(
    in FullScreenVS2PS input, 
    out float4 outColor : SV_Target0)
{
    Texture2D<float4> inputTexture = TBindless(Texture2D, float4, pushConsts.textureId);
    SamplerState pointClampSampler = Bindless(SamplerState, pushConsts.pointClampSamplerId);

    float4 sampleColor = inputTexture.Sample(pointClampSampler, input.uv);


    float3 colorAp1 = mul(sRGB_2_AP1, sampleColor.xyz);

    colorAp1 = filmToneMap(
        colorAp1, 
        0.91,  // float filmSlope, 
        0.55,  // float filmToe, 
        0.26,  // float filmShoulder, 
        0.00,  // float filmBlackClip, 
        0.04,  // float filmWhiteClip,
        0.96,  // float filmPreDesaturate,
        0.93,  // float filmPostDesaturate,
        1.00,  // float filmRedModifier,
        1.00); //float filmGlowScale

    float3 srgbColor = mul(AP1_2_sRGB, colorAp1);

    // Store linear srgb color in 10bit unorm color, do gamma encode when blit to ui.
    outColor.xyz = srgbColor;
    outColor.w   = 1.0;
} 

#endif