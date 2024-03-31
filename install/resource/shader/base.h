#pragma once

#ifdef __cplusplus /////////////////////////////////////////
    
    #include <utils/utils.h>

    namespace chord
    {
        constexpr auto kMaxPushConstSize = 128;
    }

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

    // Cpp end check struct size match machine.
    #define CHORD_PUSHCONST(Type, Name) static_assert(sizeof(Type) <= chord::kMaxPushConstSize)
    #define CHORD_DEFAULT_COMPARE(T) bool operator==(const T&) const = default

#else  ///////////////////////////////////////////////////////

    // HLSL is declaration pushconst.
    #define CHORD_PUSHCONST(Type, Name) [[vk::push_constant]] Type Name
    #define CHORD_DEFAULT_COMPARE(T)

#endif ///////////////////////////////////////////////////////
