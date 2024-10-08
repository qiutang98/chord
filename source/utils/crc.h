#pragma once

#include <cstdint>
#include <type_traits>

#include <utils/utils.h>

namespace chord::crc
{
	// Crc memory hash based on data's memory, so, you must ensure struct init with T a = {};
	extern uint32 crc32(const void* data, uint32 length, uint32 crc);

    // Single object crc hash.
    template<typename T> static inline uint32 crc32(const T& data, uint32 crc)
    {
        static_assert(std::is_object_v<T>);
        return crc32(&data, sizeof(T), crc);
    }
}