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

    namespace chord
    {
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

        inline float pow(const float base, float v)
        {
            return math::pow(base, v);
        }
    }

    // Cpp end check struct size match machine.
    #define CHORD_PUSHCONST(Type, Name) \
        static_assert(sizeof(Type) <= chord::kMaxPushConstSize)

    // Serialization relative.
    #define CHORD_DEFAULT_COMPARE_ARCHIVE(T)       \
        ARCHIVE_DECLARE                            \
        bool operator==(const T&) const = default;
        
    // GPU size require struct size pad 4 float.
    #define CHORD_CHECK_SIZE_GPU_SAFE(X) \
        static_assert(sizeof(X) % (4 * sizeof(float)) == 0)

    static inline float asuint(float floatValue)
    {
        return *reinterpret_cast<uint*>(&floatValue);
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

struct PerframeCameraView
{
    float4x4 translatedWorldToView;
    float4x4 viewToTranslatedWorld;

    float4x4 viewToClip;
    float4x4 clipToView;

    float4x4 translatedWorldToClip;
    float4x4 clipToTranslatedWorld;
};
CHORD_CHECK_SIZE_GPU_SAFE(PerframeCameraView);

// Per-object descriptor in GPU, collected every frame.
struct GPUObjectBasicData
{
    float4x4 localToWorld;
    float4x4 lastFrameLocalToWorld;
};

struct GPUObjectGLTFPrimitive
{
    GPUObjectBasicData basicData;
    uint GLTFPrimitiveDetail;
    uint pad0;
    uint pad1;
    uint pad2;
};

#endif // !SHADER_BASE_H