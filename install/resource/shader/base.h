#pragma once

#ifdef __cplusplus

    #include <utils/utils.h>
    namespace chord
    {
        static const int kMaxPushConstSize = 128;
    }

    using uint = chord::uint32;

    using float2 = chord::math::vec2;
    using float3 = chord::math::vec3;
    using float4 = chord::math::vec4;

    using float2x2 = chord::math::mat2;
    using float3x3 = chord::math::mat3;
    using float4x4 = chord::math::mat4;

    #define CHORD_PUSHCONST(Type, Name) static_assert(sizeof(Type) <= chord::kMaxPushConstSize)
#else 
    #define CHORD_PUSHCONST(Type, Name) [[vk::push_constant]] Type Name
#endif 
