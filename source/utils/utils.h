#pragma once

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <iostream>

#define ENABLE_LOG

#pragma warning(disable:4005)

#if defined(_DEBUG) || defined(DEBUG)
	#define APP_DEBUG
#endif

#ifdef APP_DEBUG
	#define DEBUG_BREAK() __debugbreak();
#else
	#define DEBUG_BREAK()
#endif 

// Optional restrict keyword for memory optimize.
#ifdef DISABLE_RESTRICT
	#define restrict_t 
#else
	#define restrict_t __restrict
#endif

// Operator enum flags support macro for enum class.
#define ENUM_CLASS_FLAG_OPERATORS(T)                                              \
    static inline T operator |(T a, T b) { return T(uint32_t(a) | uint32_t(b)); } \
    static inline T operator &(T a, T b) { return T(uint32_t(a) & uint32_t(b)); } \
    static inline T operator ~(T a     ) { return T(~uint32_t(a)); }              \
    static inline bool operator  !(T  a) { return uint32_t(a) == 0; }             \
    static inline bool operator ==(T a, uint32_t b) { return uint32_t(a) == b; }  \
    static inline bool operator !=(T a, uint32_t b) { return uint32_t(a) != b; }

namespace chord
{
	// Signed type.
	using int32  = int32_t;
	using int64  = int64_t;
	using int8   = int8_t;
	using int16  = int16_t;

	// Unsigned type.
	using uint32 = uint32_t;
	using uint64 = uint64_t;
	using uint8  = uint8_t;
	using uint16 = uint16_t;

	// Basic type platform memory check.
	static_assert(
		sizeof(int32)  == 4 && 
		sizeof(uint32) == 4 &&
		sizeof(int64)  == 8 && 
		sizeof(uint64) == 8 &&
		sizeof(int8)   == 1 &&
		sizeof(uint8)  == 1);

	// CPU cache line size, set 64 bytes here.
	constexpr uint32 kCpuCachelineSize = 64;

    // Zero object memory.
    template<typename T> static inline void zeroMemory(T& data)
	{
		static_assert(std::is_object_v<T>);
	    ::memset(&data, 0, sizeof(T));
	}

	static inline void setApplicationTitle(const std::string_view title)
	{
		std::cout << "\033]0;" << title << "\007";
	}
}
