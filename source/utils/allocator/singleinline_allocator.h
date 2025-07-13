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
				free();

				m_size = size;
				if (size > kMaxStackSize)
				{
					pPtr = traceMalloc(size);
					return pPtr;
				}
			}
			return (void*)buffer;
		}

		// Free memory.
		void free()
		{
			if (m_size > kMaxStackSize)
			{
				traceFree(pPtr, m_size);
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
				std::memcpy(allocate(other.m_size), other.getAllocation(), other.m_size);
			}
			m_size = other.m_size;
		}

		~SingleInlineAllocator() noexcept
		{
			free();
		}

		SingleInlineAllocator& operator=(const SingleInlineAllocator& other)
		{
			if (other.hasAllocation())
			{
				std::memcpy(allocate(other.m_size), other.getAllocation(), other.m_size);
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
				std::memcpy(buffer, other.buffer, m_size);
			}
		}

		// Move copy function.
		SingleInlineAllocator& operator=(SingleInlineAllocator&& other) noexcept
		{
			free();

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
				std::memcpy(buffer, other.buffer, m_size);
			}
			return *this;
		}
	};
}