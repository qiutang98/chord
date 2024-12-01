#pragma once

#include "base.hlsli"

// https://www.shadertoy.com/view/MdfSDH
// The MIT License
// Copyright © 2014 Inigo Quilez
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#if 0

// slow version, but true to the mathematical formulation
void SH_AddLightDirectional(inout float3 sh[9], in float3 col, in float3 v)
{
    #define NO  1.0        // for perfect overal brigthness match
//  #define NO (16.0/17.0) // for normalizing to maximum = 1.0;

    sh[0] += col * (NO*kPI*1.000) * (0.50*sqrt( 1.0/kPI));
    sh[1] += col * (NO*kPI*0.667) * (0.50*sqrt( 3.0/kPI)) * v.x;
    sh[2] += col * (NO*kPI*0.667) * (0.50*sqrt( 3.0/kPI)) * v.y;
    sh[3] += col * (NO*kPI*0.667) * (0.50*sqrt( 3.0/kPI)) * v.z;
    sh[4] += col * (NO*kPI*0.250) * (0.50*sqrt(15.0/kPI)) * v.x*v.z;
    sh[5] += col * (NO*kPI*0.250) * (0.50*sqrt(15.0/kPI)) * v.z*v.y;
    sh[6] += col * (NO*kPI*0.250) * (0.50*sqrt(15.0/kPI)) * v.y*v.x;
    sh[7] += col * (NO*kPI*0.250) * (0.25*sqrt( 5.0/kPI)) * (3.0*v.z*v.z-1.0);
    sh[8] += col * (NO*kPI*0.250) * (0.25*sqrt(15.0/kPI)) * (v.x*v.x-v.y*v.y);

    #undef NO
}

float3 SH_Evalulate(in float3 v, in float3 sh[9])
{
    return sh[0] * (0.50*sqrt( 1.0/kPI)) +
           sh[1] * (0.50*sqrt( 3.0/kPI)) * v.x +
           sh[2] * (0.50*sqrt( 3.0/kPI)) * v.y +
           sh[3] * (0.50*sqrt( 3.0/kPI)) * v.z +
           sh[4] * (0.50*sqrt(15.0/kPI)) * v.x*v.z +
           sh[5] * (0.50*sqrt(15.0/kPI)) * v.z*v.y +
           sh[6] * (0.50*sqrt(15.0/kPI)) * v.y*v.x +
           sh[7] * (0.25*sqrt( 5.0/kPI)) * (3.0*v.z*v.z-1.0) +
           sh[8] * (0.25*sqrt(15.0/kPI)) * (v.x*v.x-v.y*v.y);
}

#else

//
// fast version, premultiplied components and simplified terms
//
void SH_AddLightDirectional(inout float3 sh[9], in float3 col, in float3 v)
{
    #define DI 64.0  // for perfect overal brigthness match
//  #define DI 68.0  // for normalizing to maximum = 1.0;
	
	sh[0] += col * (21.0/DI);
	sh[0] -= col * (15.0/DI) * v.z*v.z;
	sh[1] += col * (32.0/DI) * v.x;
	sh[2] += col * (32.0/DI) * v.y;
	sh[3] += col * (32.0/DI) * v.z;
	sh[4] += col * (60.0/DI) * v.x*v.z;
	sh[5] += col * (60.0/DI) * v.z*v.y;
	sh[6] += col * (60.0/DI) * v.y*v.x;
	sh[7] += col * (15.0/DI) * (3.0*v.z*v.z-1.0);
	sh[8] += col * (15.0/DI) * (v.x*v.x-v.y*v.y);

    #undef DI
}

float3 SH_Evalulate(in float3 v, in float3 sh[9])
{
	return sh[0] +
           sh[1] * v.x +
           sh[2] * v.y +
           sh[3] * v.z +
           sh[4] * v.x*v.z +
           sh[5] * v.z*v.y +
           sh[6] * v.y*v.x +
           sh[7] * v.z*v.z +
           sh[8] *(v.x*v.x-v.y*v.y);
}
#endif