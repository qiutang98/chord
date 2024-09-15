#ifndef SHADER_BASE_H
#define SHADER_BASE_H

#ifdef __cplusplus /////////////////////////////////////////
    
    #include <utils/utils.h>

    using namespace chord;
    using namespace chord::math;

    // Type alias bool.
    using bool2 = chord::math::bvec2;
    using bool3 = chord::math::bvec3;
    using bool4 = chord::math::bvec4;

    // Type alias uint.
    using uint  = chord::uint32;
    using uint2 = chord::math::uvec2;
    using uint3 = chord::math::uvec3;
    using uint4 = chord::math::uvec4;

    // Type alias int.
    using int2 = chord::math::ivec2;
    using int3 = chord::math::ivec3;
    using int4 = chord::math::ivec4;

    // Type alias floats.
    using float2 = chord::math::vec2;
    using float3 = chord::math::vec3;
    using float4 = chord::math::vec4;

    // Type alias matrix.
    using float2x2 = chord::math::mat2;
    using float3x3 = chord::math::mat3;
    using float4x4 = chord::math::mat4;
    
    constexpr auto kMaxPushConstSize = 128;

    inline float4 lerp(const float4& x, const float4& y, const float4& a) { return math::mix(x, y, a); }
    inline float3 lerp(const float3& x, const float3& y, const float3& a) { return math::mix(x, y, a); }
    inline float3 lerp(const float3& x, const float3& y, const float   a) { return math::mix(x, y, a); }
    inline float2 lerp(const float2& x, const float2& y, const float2& a) { return math::mix(x, y, a); }
    inline float  lerp(const float   x, const float   y, const float   a) { return math::mix(x, y, a); }

    inline float3 pow(const float3& base, float v)
    {
        return math::pow(base, float3(v));
    }

    // Cpp end check struct size match machine.
    #define CHORD_PUSHCONST(Type, Name) \
        static_assert(sizeof(Type) <= kMaxPushConstSize)

    // Serialization relative.
    #define CHORD_DEFAULT_COMPARE_ARCHIVE(T)       \
        ARCHIVE_DECLARE                            \
        bool operator==(const T&) const = default;
        
    // GPU size require struct size pad 4 float.
    #define CHORD_CHECK_SIZE_GPU_SAFE(X) \
        static_assert(sizeof(X) % (4 * sizeof(float)) == 0)

    static inline uint asuint(float floatValue)
    {
        return *reinterpret_cast<uint*>(&floatValue);
    }

    static inline void asuint(double doubleValue, uint& lowbits, uint& highbits)
    {
        const uint* data = (uint*)(&doubleValue);

        lowbits  = data[0];
        highbits = data[1];
    }

    static inline float asfloat(uint32 uintValue)
    {
        return *reinterpret_cast<float*>(&uintValue);
    }

#else  ///////////////////////////////////////////////////////

    // HLSL is declaration pushconst.
    #define CHORD_PUSHCONST(Type, Name) [[vk::push_constant]] Type Name
    #define CHORD_DEFAULT_COMPARE_ARCHIVE(T)
    #define CHORD_CHECK_SIZE_GPU_SAFE(X)
    #define ARCHIVE_DECLARE

#endif ///////////////////////////////////////////////////////

struct DensityProfileLayer 
{
    float width;
    float exp_term;
    float exp_scale;
    float linear_term;

    float constant_term;
    float pad0;
    float pad1;
    float pad2;
};

struct DensityProfile 
{
    DensityProfileLayer layers[2];
};

#define LUMINANCE_MODE_NONE       0
#define LUMINANCE_MODE_APPROX     1
#define LUMINANCE_MODE_PRECOMPUTE 2
struct AtmosphereParameters 
{
    // The solar irradiance at the top of the atmosphere.
    float3 solar_irradiance;
    // The sun's angular radius. Warning: the implementation uses approximations
    // that are valid only if this angle is smaller than 0.1 radians.
    float sun_angular_radius;

    // The distance between the planet center and the bottom of the atmosphere.
    float bottom_radius;
    // The distance between the planet center and the top of the atmosphere.
    float top_radius;
    uint  luminanceMode; // 0 none, 1 approx, 2 precompute.
    float pad1;

    // The density profile of air molecules, i.e. a function from altitude to
    // dimensionless values between 0 (null density) and 1 (maximum density).
    DensityProfile rayleigh_density;
    // The scattering coefficient of air molecules at the altitude where their
    // density is maximum (usually the bottom of the atmosphere), as a function of
    // wavelength. The scattering coefficient at altitude h is equal to
    // 'rayleigh_scattering' times 'rayleigh_density' at this altitude.
    float3 rayleigh_scattering;
    uint bCombineScattering;

    // The density profile of aerosols, i.e. a function from altitude to
    // dimensionless values between 0 (null density) and 1 (maximum density).
    DensityProfile mie_density;
    // The scattering coefficient of aerosols at the altitude where their density
    // is maximum (usually the bottom of the atmosphere), as a function of
    // wavelength. The scattering coefficient at altitude h is equal to
    // 'mie_scattering' times 'mie_density' at this altitude.
    float3 mie_scattering;
    float pad3;

    // The extinction coefficient of aerosols at the altitude where their density
    // is maximum (usually the bottom of the atmosphere), as a function of
    // wavelength. The extinction coefficient at altitude h is equal to
    // 'mie_extinction' times 'mie_density' at this altitude.
    float3 mie_extinction;
    // The asymetry parameter for the Cornette-Shanks phase function for the
    // aerosols.
    float mie_phase_function_g;

    // The density profile of air molecules that absorb light (e.g. ozone), i.e.
    // a function from altitude to dimensionless values between 0 (null density)
    // and 1 (maximum density).
    DensityProfile absorption_density;
    // The extinction coefficient of molecules that absorb light (e.g. ozone) at
    // the altitude where their density is maximum, as a function of wavelength.
    // The extinction coefficient at altitude h is equal to
    // 'absorption_extinction' times 'absorption_density' at this altitude.
    float3 absorption_extinction;
    float pad4;

    // The average albedo of the ground.
    float3 ground_albedo;
    // The cosine of the maximum Sun zenith angle for which atmospheric scattering
    // must be precomputed (for maximum precision, use the smallest Sun zenith
    // angle yielding negligible sky light radiance values. For instance, for the
    // Earth case, 102 degrees is a good choice - yielding mu_s_min = -0.2).
    float mu_s_min;

    float3 skySpectralRadianceToLumiance;
    float pad5;

    float3 sunSpectralRadianceToLumiance;
    float pad6;

    float3 earthCenterKm;
    float pad7;
};

struct SkyLightInfo
{
    float3 direction;
    float  pad0;
};
CHORD_CHECK_SIZE_GPU_SAFE(SkyLightInfo);

struct GPUBasicData
{
    AtmosphereParameters atmosphere;
    SkyLightInfo sunInfo;

    uint frameCounter; // 32 bit-framecounter, easy overflow. todo:
    uint frameCounterMod8;
    uint GLTFPrimitiveDetailBuffer; // gpu scene: gltf primitive detail buffer index.
    uint GLTFPrimitiveDataBuffer; // gpu scene: gltf primitive datas buffer index.
    
    uint GLTFMaterialBuffer; 
    uint GLTFObjectCount; // scene: total gltf object count.
    uint GLTFObjectBuffer; // scene: gltf object buffer index.
    uint debuglineVertices; // debugline store vertices buffer index.
    
    uint debuglineCount; // current debug line use count buffer index.
    uint debuglineMaxCount; // maximum of debug line can use.
    uint pointClampEdgeSampler; // All point clamp edge sampler. 
    uint pad2;
};

struct GPUStorageDouble4
{
    uint2 x;
    uint2 y;
    uint2 z;
    uint2 w;
};

struct PerframeCameraView
{
    GPUBasicData basicData;

    // float4x4 localToClip = mul(mul(viewToClip, translatedWorldToView), localToTranslatedWorld);
    float4x4 translatedWorldToView;
    float4x4 viewToTranslatedWorld;

    float4x4 viewToClip;
    float4x4 clipToView;

    float4x4 translatedWorldToClip;
    float4x4 clipToTranslatedWorld;

    float4 frustumPlane[6]; // World space frustum plane.

    float4x4 translatedWorldToClipLastFrame;
    float4 frustumPlaneLastFrame[6];

    float4 renderDimension; //  .xy is width and height, .zw is inverse of .xy.

    GPUStorageDouble4 cameraToEarthCenterKm; // .xyz store cameraPosition - earthCenter.
    GPUStorageDouble4 cameraRelativeEarthKm; // Camera pos in km.

    // Camera world position, double precision.
    GPUStorageDouble4 cameraWorldPos;

    float cameraFovy;
    float pad0;
    float pad1;
    float pad2;
};
CHORD_CHECK_SIZE_GPU_SAFE(PerframeCameraView);

// Per-object descriptor in GPU, collected every frame.
struct GPUObjectBasicData
{
    float4x4 localToTranslatedWorld;
    float4x4 translatedWorldToLocal; // inverse of localToTranslatedWorld

    float4x4 localToTranslatedWorldLastFrame;

    float4 scaleExtractFromMatrix; // .xyz is scale, .w is largest abs(.xyz)
};

struct GPUObjectGLTFPrimitive
{
    GPUObjectBasicData basicData;
    uint GLTFPrimitiveDetail;
    uint GLTFMaterialData;
    uint pad1;
    uint pad2;
};

inline uint shaderSetFlag(uint flags, uint bit)
{
    return flags | (1U << bit);
}

inline bool shaderHasFlag(uint flags, uint bit)
{
    return (flags & (1U << bit)) != 0U;
}

struct LineDrawVertex
{
    float3 translatedWorldPos;
    uint color; // 8 bit per component, .rgba
};



inline uint shaderPackColor(uint R, uint G, uint B, uint A)
{
    uint color = 0;
    color |= (R & 0xFF) <<  0;
    color |= (G & 0xFF) <<  8;
    color |= (B & 0xFF) << 16;
    color |= (A & 0xFF) << 24;
    return color;
}

inline uint shaderPackColor(uint4 c)
{
    return shaderPackColor(c.x, c.y, c.z, c.w);
}

inline float4 shaderUnpackColor(uint packData)
{
    float4 color;
    color.r = float((packData >>  0) & 0xFF) / 255.0;
    color.g = float((packData >>  8) & 0xFF) / 255.0;
    color.b = float((packData >> 16) & 0xFF) / 255.0;
    color.a = float((packData >> 24) & 0xFF) / 255.0;

    return color;
}

// HZB mipmap count max 12, meaning from 4096 - 1.
#define kHZBMaxMipmapCount 12

// 24 bit for max instance id count.
#define kMaxInstanceIdCount 0xFFFFFF

// Max shading type is 127.
#define kMaxShadingType  0x7F

// Shading type list, max is 127.
enum class EShadingType
{
    None = 0,
    GLTF_MetallicRoughnessPBR = 1,
    MAX
};
#define kLightingType_None 0
#define kLightingType_GLTF_MetallicRoughnessPBR 1

#define kWaveSize 32

// Nanite config: fit mesh shader. Best performance: 126 triangles and 64 vertices.
#define kNaniteMeshletMaxVertices 255
#define kNaniteMeshletMaxTriangle 128
#define kNaniteMaxLODCount        12
#define kNaniteMaxBVHLevelCount   14
#define kNaniteBVHLevelNodeCount  8


// NV 3070Ti, see VkPhysicalDeviceLimits::subpixelPrecisionBits
#define subpixelPrecisionBits  8
#define subpixelPrecisionCount (1 << subpixelPrecisionBits)
#define subpixel

struct NaniteShadingMeshlet
{
    uint shadingPack; // ShadingType: 8bit, Material Id: 24bit.
};


// smallest such that 1.0 + kEpsilon != 1.0
#define kEpsilon 1.192092896e-07F

// 
#define kUnvalidIdUint32 0xFFFFFFFFu

// Draw command for gltf meshlet, used for vertex shader fallback.
struct GLTFMeshletDrawCmd
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
    uint objectId;
    uint meshletId;
    uint instanceId;
};

#endif // !SHADER_BASE_H