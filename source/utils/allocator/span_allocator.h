#pragma once

#include <memory>
#include <cstdint>
#include <vector>

#include <utils/memory.h>

#include <utils/noncopyable.h>
#include <utils/tagged_ptr.h>
#include <utils/profiler.h>

namespace chord
{
	// Free list based spare linear memory allocator. 
	class SpanAllocator
	{
	public:
		explicit SpanAllocator(bool bGrowOnly);

		int32_t allocate(uint32_t size);
		void free(uint32_t offset, uint32_t size);

		// 
		void prune();
		void clear();

		inline auto getMaxSize() const
		{
			return m_bGrowOnly ? m_peakMaxSize : m_currentMaxSize;
		}

	private:
		// Search in m_freeSpans which fit the size required, return -1 if no span found.
		int32_t searchFreeList(uint32_t offset, uint32_t size) const;

	private:
		const bool m_bGrowOnly;

		// Size of linear range used by the allocator.
		uint32_t m_currentMaxSize;

		// 
		uint32_t m_peakMaxSize;

		// 
		uint32_t m_firstNonEmptySpan;

		struct Allocation
		{
			uint32_t offset;
			uint32_t size;
		};
		std::vector<Allocation> m_freeSpans;
		std::vector<Allocation> m_pendingFreeSpans;
	};
}