#include "allocator.h"
#include "log.h"

namespace chord
{
	SpanAllocator::SpanAllocator(bool bGrowOnly)
		: m_currentMaxSize(0)
		, m_peakMaxSize(0)
		, m_firstNonEmptySpan(0)
		, m_bGrowOnly(bGrowOnly)
	{
		
	}

	void SpanAllocator::clear()
	{
		m_currentMaxSize = 0;
		m_peakMaxSize = 0;
		m_firstNonEmptySpan = 0;
		m_freeSpans = {};
		m_pendingFreeSpans = {};
	}

	int32_t SpanAllocator::allocate(uint32_t size)
	{
		// 
		int32_t freeSpanIndex = searchFreeList(m_firstNonEmptySpan, size);

		// No found in m_freeSpans, try in m_pendingFreeSpans.
		if (freeSpanIndex == -1 && !m_pendingFreeSpans.empty())
		{
			prune();
			freeSpanIndex = searchFreeList(m_firstNonEmptySpan, size);
		}

		// Already found one free span, reuse.
		if (freeSpanIndex != -1)
		{
			Allocation freeSpan = m_freeSpans[freeSpanIndex];

			m_freeSpans[freeSpanIndex] = 
			{ 
				.offset = freeSpan.offset + size, 
				.size   = freeSpan.size   - size // size maybe zero, will be clean in prune.
			};

			// Update m_firstNonEmptySpan if already empty.
			if (freeSpan.size == size && m_firstNonEmptySpan == freeSpanIndex)
			{
				m_firstNonEmptySpan = freeSpanIndex + 1;
			}

			return freeSpan.offset;
		}

		// Need new allocation.
		uint32_t offset = m_currentMaxSize;

		// Update new max size and peak size.
		m_currentMaxSize += size;
		m_peakMaxSize = std::max(m_peakMaxSize, m_currentMaxSize);

		return offset;
	}

	void SpanAllocator::free(uint32_t offset, uint32_t size)
	{
		check(offset + size <= m_currentMaxSize);
		m_pendingFreeSpans.push_back(Allocation{ .offset = offset, .size = size });
	}

	int32_t SpanAllocator::searchFreeList(uint32_t offset, uint32_t size) const
	{
		for (auto index = offset; index < m_freeSpans.size(); index++)
		{
			// Use first valid span.
			if (m_freeSpans[index].size >= size)
			{
				return int32_t(index);
			}
		}

		return -1;
	}

	void SpanAllocator::prune()
	{
		if (m_pendingFreeSpans.empty() && m_firstNonEmptySpan == 0)
		{
			return;
		}

		// Sort by offset.
		std::sort(m_pendingFreeSpans.begin(), m_pendingFreeSpans.end(), [](const auto& A, const auto& B) 
		{
			return A.offset < B.offset;
		});

		std::vector<Allocation> tempFreeSpans;
		tempFreeSpans.reserve(m_freeSpans.size());

		int32_t lastEndOffset   = -1;
		uint32_t pendingFreeIndex =  0;

		constexpr uint32_t kMaxOffset = ~0U;
		for (auto index = 0; index < m_freeSpans.size() || pendingFreeIndex < m_pendingFreeSpans.size(); )
		{
			auto allocation = index < m_freeSpans.size() ? m_freeSpans[index] : Allocation { .offset = kMaxOffset, .size = 0 };
			check(pendingFreeIndex < m_pendingFreeSpans.size() || allocation.offset < kMaxOffset);

			if (pendingFreeIndex < m_pendingFreeSpans.size() && m_pendingFreeSpans[pendingFreeIndex].offset < allocation.offset)
			{
				// Load from pending spans.
				allocation = m_pendingFreeSpans[pendingFreeIndex];
				pendingFreeIndex++;
			}
			else
			{
				// Load from free spans.
				index ++;
			}
			check(allocation.offset < kMaxOffset);

			// Clean empty allocation
			if (allocation.size > 0)
			{
				if (lastEndOffset == allocation.offset)
				{
					// Fuse adjacent.
					tempFreeSpans.back().size += allocation.size;
				}
				else
				{
					tempFreeSpans.push_back(allocation);
				}
				lastEndOffset = tempFreeSpans.back().size + tempFreeSpans.back().offset;
			}
		}

		// Discard last free span to shrink.
		if (!tempFreeSpans.empty() && tempFreeSpans.back().offset + tempFreeSpans.back().size == m_currentMaxSize)
		{
			m_currentMaxSize -= tempFreeSpans.back().size;
			tempFreeSpans.pop_back();
		}

		// Now get new free spans.
		m_freeSpans = std::move(tempFreeSpans);

		// Free pending free spans.
		m_pendingFreeSpans.clear();

		// Reset first non empty span.
		m_firstNonEmptySpan = 0;
	}
}