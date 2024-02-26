#pragma once

#include <pch.h>

#include <cstdint>
#include <type_traits>
#include <atomic>
#include <iostream>

#define ENABLE_LOG
#define ENABLE_RESTRICT
#define ENABLE_NODISCARD

#if defined(_DEBUG) || defined(DEBUG)
	#define CHORD_DEBUG 1
#else
	#define CHORD_DEBUG 0
#endif

#ifdef ENABLE_NODISCARD
	#define CHORD_NODISCARD [[nodiscard]]
#else 
	#define CHORD_NODISCARD
#endif

// Optional restrict keyword for memory optimize.
#ifdef ENABLE_RESTRICT
	#define CHORD_RESTRICT __restrict
#else
	#define CHORD_RESTRICT 
#endif

#ifdef ENABLE_LOG
	#define chord_macro_sup_enableLogOnly(x) x
#else
	#define chord_macro_sup_enableLogOnly(x)
#endif

#if CHORD_DEBUG
	#define chord_macro_sup_debugOnly(x) x
	#define chord_macro_sup_checkPrintContent(x, p) \
	{ if(!(x)) { p("Check content '{3}' failed in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__, #x); } }

	#define chord_macro_sup_checkMsgfPrintContent(x, p, ...) \
	{ if(!(x)) { p("Assert content '{4}' failed with message: '{3}' in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__, std::format(__VA_ARGS__), #x); } }
#else
	#define chord_macro_sup_debugOnly(x)
	#define chord_macro_sup_checkPrintContent(x, p) { if(!(x)) { p("Check content '{0}' failed.", #x); } }
	#define chord_macro_sup_checkMsgfPrintContent(x, p, ...) { if(!(x)) { p("Assert content '{0}' failed with message '{1}'.", #x, __VA_ARGS__); } }
#endif

#define chord_macro_sup_ensureMsgfContent(x, p, ...) \
{ static bool b = false; if(!b && !(x)) { b = true; p("Ensure content '{4}' failed with message '{3}' in function '{1}' at line #{0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__, std::format(__VA_ARGS__), #x); chord::debugBreak(); } }

#define chord_macro_sup_checkEntryContent(x) x("Check entry in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__)
#define chord_macro_sup_unimplementedContent(x) x("Unimplemented code entry in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__)

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
	class ImageLdr2D;
	class Application;

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

	extern void debugBreak();

	static inline void applicationCrash()
	{
		debugBreak();
		::abort();
	}

	static inline bool same(const char* a, const char* b)
	{
		return strcmp(a, b) == 0;
	}

	template<typename T>
	constexpr const char* getTypeName()
	{
		return typeid(T).name();
	}

	template<typename T>
	constexpr size_t getTypeHash()
	{
		return typeid(T).hash_code();
	}
}
