#pragma once

#include <cstdint>
#include <type_traits>

#include <utils/utils.h>

namespace chord::crc
{
	// Crc memory hash based on data's memory, so, you must ensure struct init with T a = {};
	extern uint32_t crc32(const void* data, uint32_t length, uint32_t crc = 0);

    // Single object crc hash.
    template<typename T> static inline uint32_t crc32(const T& data, uint32_t crc = 0)
    {
        static_assert(std::is_object_v<T>);
        return crc32(&data, sizeof(T), crc);
    }
}