#ifndef ACES_HLSLI
#define ACES_HLSLI

#include "colorspace.h"
#include "base.hlsli"

/**
***************************************************************************************
	ACES: Academy Color Encoding System
	https://github.com/ampas/aces-dev/tree/v1.0

	License Terms for Academy Color Encoding System Components

	Academy Color Encoding System (ACES) software and tools are provided by the Academy under
	the following terms and conditions: A worldwide, royalty-free, non-exclusive right to copy, modify, create
	derivatives, and use, in source and binary forms, is hereby granted, subject to acceptance of this license.

	Copyright © 2013 Academy of Motion Picture Arts and Sciences (A.M.P.A.S.). Portions contributed by
	others as indicated. All rights reserved.

	Performance of any of the aforementioned acts indicates acceptance to be bound by the following
	terms and conditions:

	 *	Copies of source code, in whole or in part, must retain the above copyright
		notice, this list of conditions and the Disclaimer of Warranty.
	 *	Use in binary form must retain the above copyright notice, this list of
		conditions and the Disclaimer of Warranty in the documentation and/or other
		materials provided with the distribution.
	 *	Nothing in this license shall be deemed to grant any rights to trademarks,
		copyrights, patents, trade secrets or any other intellectual property of
		A.M.P.A.S. or any contributors, except as expressly stated herein.
	 *	Neither the name "A.M.P.A.S." nor the name of any other contributors to this
		software may be used to endorse or promote products derivative of or based on
		this software without express prior written permission of A.M.P.A.S. or the
		contributors, as appropriate.

	This license shall be construed pursuant to the laws of the State of California,
	and any disputes related thereto shall be subject to the jurisdiction of the courts therein.

	Disclaimer of Warranty: THIS SOFTWARE IS PROVIDED BY A.M.P.A.S. AND CONTRIBUTORS "AS
	IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
	NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL A.M.P.A.S., OR ANY
	CONTRIBUTORS OR DISTRIBUTORS, BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	SPECIAL, EXEMPLARY, RESITUTIONARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
	NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
	DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
	OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
	NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
	EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

	WITHOUT LIMITING THE GENERALITY OF THE FOREGOING, THE ACADEMY SPECIFICALLY
	DISCLAIMS ANY REPRESENTATIONS OR WARRANTIES WHATSOEVER RELATED TO PATENT OR
	OTHER INTELLECTUAL PROPERTY RIGHTS IN THE ACADEMY COLOR ENCODING SYSTEM, OR
	APPLICATIONS THEREOF, HELD BY PARTIES OTHER THAN A.M.P.A.S.,WHETHER DISCLOSED
	OR UNDISCLOSED.
***************************************************************************************
*/

// Sigmoid function in the range 0 to 1 spanning -2 to +2.
float sigmoid_shaper(float x) 
{ 
	float t = max(1.0 - abs(0.5 * x), 0.0);
	float y = 1.0 + sign(x) * (1.0 - t * t);

	return 0.5 * y;
}

float rgb_2_saturation(float3 rgb) 
{
	float minrgb = min(min(rgb.r, rgb.g), rgb.b);
	float maxrgb = max(max(rgb.r, rgb.g), rgb.b);
	return (max(maxrgb, 1e-10) - max(minrgb, 1e-10)) / max(maxrgb, 1e-2);
}

// Converts RGB to a luminance proxy, here called YC. YC is ~ Y + K * Chroma.
float rgb_2_yc(float3 rgb, float ycRadiusWeight) 
{ 
	float chroma = sqrt(rgb.b * (rgb.b - rgb.g) + rgb.g * (rgb.g - rgb.r) + rgb.r * (rgb.r - rgb.b));
	return (rgb.b + rgb.g + rgb.r + ycRadiusWeight * chroma) / 3.0;
}

float glow_fwd(float ycIn, float glowGainIn, float glowMid) 
{
	float glowGainOut;

	if (ycIn <= 2.0 / 3.0 * glowMid) 
    {
		glowGainOut = glowGainIn;
	} 
    else if ( ycIn >= 2.0 * glowMid) 
    {
		glowGainOut = 0;
	} 
    else 
    {
		glowGainOut = glowGainIn * (glowMid / ycIn - 0.5);
	}

	return glowGainOut;
}

// Returns a geometric hue angle in degrees (0-360) based on RGB values.
float rgb_2_hue(float3 rgb) 
{ 
	float hue;
	if (rgb[0] == rgb[1] && rgb[1] == rgb[2]) 
    { 
        // For neutral colors, hue is undefined and the function will return a quiet NaN value.
		hue = 0;
	} 
    else 
    {
		hue = (180. / kPI) * atan2(sqrt(3.0) * (rgb[1] - rgb[2]), 2 * rgb[0] - rgb[1] - rgb[2]);
	}

	if (hue < 0.0)
    {
		hue = hue + 360.0;
    }

	return clamp(hue, 0.0, 360.0);
}

float center_hue(float hue, float centerH) 
{
	float hueCentered = hue - centerH;

	if (hueCentered < -180.0) 
    {
		hueCentered += 360.0;
	} 
    else if (hueCentered > 180.0) 
    {
		hueCentered -= 360.0;
	}

	return hueCentered;
}

#endif