#pragma once

#include <memory>
#include <cstdint>
#include <vector>

namespace chord
{
	// Single object inline allocator. it keep one small stack memory.
	// When stack memory overflow, then use heap memory.
	template<std::size_t kMaxStackSize>
	class SingleInlineAllocator
	{
	private:
		static_assert(kMaxStackSize > sizeof(void*), 
			"kMaxStackSize is smaller or equal to the size of a pointer.");

		union
		{
			char buffer[kMaxStackSize];
			void* pPtr; // Heap memory, use when memory size bigger than kMaxStackSize.
		};
		std::size_t m_size;

	public:
		// Allocate memory.
		void* allocate(const std::size_t size)
		{
			if (m_size != size)
			{
				release();

				m_size = size;
				if (size > kMaxStackSize)
				{
					pPtr = ::malloc(size);
					return pPtr;
				}
			}
			return (void*)buffer;
		}

		// Free memory.
		void release()
		{
			if (m_size > kMaxStackSize)
			{
				::free(pPtr);
			}
			m_size = 0;
		}

	public:
		std::size_t getSize() const 
		{ 
			return m_size; 
		}

		bool hasAllocation() const 
		{ 
			return m_size > 0; 
		}

		bool hasHeapAllocation() const 
		{ 
			return m_size > kMaxStackSize; 
		}

		void* getAllocation() const
		{
			if (hasAllocation())
			{
				return hasHeapAllocation() ? pPtr : (void*)buffer;
			}
			else
			{
				return nullptr;
			}
		}

		SingleInlineAllocator() noexcept : m_size(0)
		{

		}

		SingleInlineAllocator(const SingleInlineAllocator& other) : m_size(0)
		{
			if (other.hasAllocation())
			{
				::memcpy(allocate(other.m_size), other.getAllocation(), other.m_size);
			}
			m_size = other.m_size;
		}

		~SingleInlineAllocator() noexcept
		{
			release();
		}

		SingleInlineAllocator& operator=(const SingleInlineAllocator& other)
		{
			if (other.hasAllocation())
			{
				::memcpy(allocate(other.m_size), other.getAllocation(), other.m_size);
			}
			m_size = other.m_size;

			return *this;
		}

		// Move construct function.
		SingleInlineAllocator(SingleInlineAllocator&& other) noexcept : m_size(other.m_size)
		{
			other.m_size = 0;

			if (m_size > kMaxStackSize)
			{
				// Heap memory just swap.
				std::swap(pPtr, other.pPtr);
			}
			else
			{
				// Stack memory use copy.
				::memcpy(buffer, other.buffer, m_size);
			}
		}

		// Move copy function.
		SingleInlineAllocator& operator=(SingleInlineAllocator&& other) noexcept
		{
			release();

			m_size = other.m_size;
			other.m_size = 0;

			if (m_size > kMaxStackSize)
			{
				// Heap memory just swap.
				std::swap(pPtr, other.pPtr);
			}
			else
			{
				// Stack memory use copy.
				::memcpy(buffer, other.buffer, m_size);
			}
			return *this;
		}
	};

	// Free list based spare linear memory allocator. 
	class SpanAllocator
	{
	public:
		explicit SpanAllocator(bool bGrowOnly);

		int32_t allocate(size_t size);
		void free(size_t offset, size_t size);

		// 
		void prune();
		void clear();

		inline auto getMaxSize() const
		{
			return m_bGrowOnly ? m_peakMaxSize : m_currentMaxSize;
		}
	
	private:
		// Search in m_freeSpans which fit the size required, return -1 if no span found.
		int32_t searchFreeList(size_t offset, size_t size) const;

	private:
		const bool m_bGrowOnly;
		
		// Size of linear range used by the allocator.
		size_t m_currentMaxSize;

		// 
		size_t m_peakMaxSize;
	
		// 
		size_t m_firstNonEmptySpan;

		struct Allocation
		{
			size_t offset;
			size_t size;
		};
		std::vector<Allocation> m_freeSpans;
		std::vector<Allocation> m_pendingFreeSpans;
	};


	class PoolAllocator
	{
	public:
		explicit PoolAllocator(size_t elementSize)
			: m_elementSize(elementSize)
			, m_currentMaxSize(0)
		{

		}

		size_t allocate()
		{
			if (m_freeIds.empty())
			{
				return m_currentMaxSize ++;
			}

			size_t result = m_freeIds.top();
			m_freeIds.pop();
			return result;
		}

		void free(size_t id)
		{
			m_freeIds.push(id);
		}

		size_t getMaxSize() const
		{
			return m_currentMaxSize;
		}

	private:
		size_t m_currentMaxSize;
		size_t m_elementSize;
		std::stack<size_t> m_freeIds;
	};
}