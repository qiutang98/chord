#pragma once

#include <utils/utils.h>
#include <utils/log.h>
#include <utils/optional.h>

// Texture in vulkan use bindless id.
#define ImTextureID chord::uint32

// Romote assert to engine assert.
#define IM_ASSERT(_EXPR)  check(_EXPR)

// Remote debug break to engine debug break.
#define IM_DEBUG_BREAK chord::debugBreak

// Use math::vec2 as ImVec2 pass type.
#define IM_VEC2_CLASS_EXTRA  \
    constexpr ImVec2(const chord::math::vec2& f) : x(f.x), y(f.y) {} \
    operator chord::math::vec2() const { return chord::math::vec2(x,y); }

// Use math::vec4 as ImVec4 pass type.
#define IM_VEC4_CLASS_EXTRA \
    constexpr ImVec4(const chord::math::vec4& f) : x(f.x), y(f.y), z(f.z), w(f.w) {} \
    operator chord::math::vec4() const { return chord::math::vec4(x,y,z,w); }