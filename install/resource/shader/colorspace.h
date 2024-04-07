#pragma once

#include "base.h"

namespace chord
{
    float3 rec709GammaDecode(float3 gammaRec709)
    {
        return lerp(gammaRec709 / 12.92f, pow((gammaRec709 + .055f) / 1.055f, 2.4f), step(0.04045f, gammaRec709));
    }

    float3 rec709GammaEncode(float3 linearRec709)
    {
        return lerp(12.92f * linearRec709, 1.055f * pow(linearRec709, 0.41666f) - 0.055f, step(0.0031308f, linearRec709));
    }
}