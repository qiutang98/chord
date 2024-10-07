#ifndef COLORSPACE_H
#define COLORSPACE_H

#include "base.h"

static const float3 D65_WHITE = float3(0.95045592705, 1.0, 1.08905775076);
static const float3 D50_WHITE = float3(0.96429567643, 1.0, 0.82510460251);

static const float3x3 sRGB_2_XYZ_MAT =
{
    0.4123907993, 0.3575843394, 0.1804807884,
    0.2126390059, 0.7151686788, 0.0721923154,
    0.0193308187, 0.1191947798, 0.9505321522,
};

static const float3x3 XYZ_2_sRGB_MAT =
{
    +3.2409699419, -1.5373831776, -0.4986107603,
    -0.9692436363, +1.8759675015, +0.0415550574,
    +0.0556300797, -0.2039769589, +1.0569715142,
};

static const float3x3 AP0_2_XYZ_MAT =
{
    0.9525523959, 0.0000000000, +0.0000936786,
    0.3439664498, 0.7281660966, -0.0721325464,
    0.0000000000, 0.0000000000, +1.0088251844,
};

static const float3x3 XYZ_2_AP0_MAT =
{
    +1.0498110175, 0.0000000000, -0.0000974845,
    -0.4959030231, 1.3733130458, +0.0982400361,
    +0.0000000000, 0.0000000000, +0.9912520182,
};

static const float3x3 AP1_2_XYZ_MAT =
{
    +0.6624541811, 0.1340042065, 0.1561876870,
    +0.2722287168, 0.6740817658, 0.0536895174,
    -0.0055746495, 0.0040607335, 1.0103391003,
};

static const float3x3 XYZ_2_AP1_MAT =
{
    +1.6410233797, -0.3248032942, -0.2364246952,
    -0.6636628587,  1.6153315917,  0.0167563477,
    +0.0117218943, -0.0082844420,  0.9883948585,
};

static const float3x3 AP0_2_AP1_MAT = //mul(AP0_2_XYZ_MAT, XYZ_2_AP1_MAT);
{
    +1.4514393161, -0.2365107469, -0.2149285693,
    -0.0765537734,  1.1762296998, -0.0996759264,
    +0.0083161484, -0.0060324498,  0.9977163014,
};

static const float3x3 AP1_2_AP0_MAT = //mul(AP1_2_XYZ_MAT, XYZ_2_AP0_MAT);
{
    +0.6954522414,  0.1406786965,  0.1638690622,
    +0.0447945634,  0.8596711185,  0.0955343182,
    -0.0055258826,  0.0040252103,  1.0015006723,
};

static const float3 AP1_RGB2Y =
{
    0.2722287168, //AP1_2_XYZ_MAT[0][1],
    0.6740817658, //AP1_2_XYZ_MAT[1][1],
    0.0536895174, //AP1_2_XYZ_MAT[2][1]
};

// ACES work in white point D60.
// sRGB work in white point D65
static const float3x3 D65_2_D60_CAT =
{
    +1.0130349146, 0.0061052578, -0.0149709436,
    +0.0076982301, 0.9981633521, -0.0050320385,
    -0.0028413174, 0.0046851567,  0.9245061375,
};
static const float3x3 D60_2_D65_CAT =
{
    +0.9872240087, -0.0061132286, 0.0159532883,
    -0.0075983718,  1.0018614847, 0.0053300358,
    +0.0030725771, -0.0050959615, 1.0816806031,
};


// Rec2020 color space
static const float3x3 XYZ_2_Rec2020_MAT =
{
    +1.7166511880, -0.3556707838, -0.2533662814,
    -0.6666843518,  1.6164812366,  0.0157685458,
    +0.0176398574, -0.0427706133,  0.9421031212,
};

static const float3x3 Rec2020_2_XYZ_MAT =
{
    0.6369580483, 0.1446169036, 0.1688809752,
    0.2627002120, 0.6779980715, 0.0593017165,
    0.0000000000, 0.0280726930, 1.0609850577,
};

#ifndef __cplusplus
static const float3x3 sRGB_2_AP1 = mul(XYZ_2_AP1_MAT,  mul(D65_2_D60_CAT, sRGB_2_XYZ_MAT));
static const float3x3 AP1_2_sRGB = mul(XYZ_2_sRGB_MAT, mul(D60_2_D65_CAT,  AP1_2_XYZ_MAT));
static const float3x3 AP0_2_AP1  = mul(XYZ_2_AP1_MAT, AP0_2_XYZ_MAT);
static const float3x3 AP1_2_AP0  = mul(XYZ_2_AP0_MAT, AP1_2_XYZ_MAT);
#endif 

#ifdef __cplusplus
inline float rec709GammaDecode(float gammaRec709)
{
    using namespace chord;
    return math::lerp(gammaRec709 / 12.92f, pow((gammaRec709 + .055f) / 1.055f, 2.4f), math::step(0.04045f, gammaRec709));
}
#else
inline float rec709GammaDecode(float gammaRec709)
{
    return lerp(gammaRec709 / 12.92f, pow((gammaRec709 + .055f) / 1.055f, 2.4f), step(0.04045f, gammaRec709));
}
#endif

#ifdef __cplusplus
inline float rec709GammaEncode(float linearRec709)
{
    using namespace chord;
    return math::lerp(12.92f * linearRec709, 1.055f * pow(linearRec709, 0.41666f) - 0.055f, math::step(0.0031308f, linearRec709));
}
#else
inline float rec709GammaEncode(float linearRec709)
{
    return lerp(12.92f * linearRec709, 1.055f * pow(linearRec709, 0.41666f) - 0.055f, step(0.0031308f, linearRec709));
}
#endif

#ifdef __cplusplus
inline float3 rec709GammaDecode(float3 gammaRec709)
{
    using namespace chord;
    return chord::math::lerp(gammaRec709 / 12.92f, math::pow((gammaRec709 + .055f) / 1.055f, float3(2.4f)), math::step(0.04045f, gammaRec709));
}
#else
inline float3 rec709GammaDecode(float3 gammaRec709)
{
    return lerp(gammaRec709 / 12.92f, pow((gammaRec709 + .055f) / 1.055f, 2.4f), step(0.04045f, gammaRec709));
}
#endif 

#ifdef __cplusplus
inline float3 rec709GammaEncode(float3 linearRec709)
{
    using namespace chord;
    return math::lerp(12.92f * linearRec709, 1.055f * math::pow(linearRec709, float3(0.41666f)) - 0.055f, math::step(0.0031308f, linearRec709));
}
#else
inline float3 rec709GammaEncode(float3 linearRec709)
{
    return lerp(12.92f * linearRec709, 1.055f * pow(linearRec709, 0.41666f) - 0.055f, step(0.0031308f, linearRec709));
}
#endif

#endif // !COLORSPACE_H