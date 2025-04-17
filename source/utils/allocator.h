#pragma once

#include <memory>
#include <cstdint>
#include <vector>

#include <utils/noncopyable.h>
#include <utils/tagged_ptr.h>

namespace chord
{
	template<typename T, size_t kPageSize>
	class LinkListArenaAllocator final : NonCopyable 
	{
		#pragma warning(push)
		#pragma warning(disable:4624)
		struct Node
		{
			union 
			{
				T data { };
				Node* next;
			};
		};
		#pragma warning(pop)

		using TPointer = TaggedPointer<Node>;
		static_assert(sizeof(Node) < kPageSize);
		static constexpr uint32 kElementCount = kPageSize / sizeof(Node);

		alignas(kCpuCachelineSize) std::atomic<TPointer> m_freeList{ };
		static_assert(std::atomic<TPointer>::is_always_lock_free);

		std::mutex m_arenaCreateMutex;
		std::vector<void*> m_arenas;

		void createArena()
		{
			void* memory = std::malloc(kPageSize);
			m_arenas.push_back(memory);

			T* newBlob = reinterpret_cast<T*>(memory);
			for (uint32 i = 0; i < kElementCount; i++)
			{
				free(&newBlob[i]);
			}
		}

	public:
		explicit LinkListArenaAllocator()
		{
			createArena();
		}

		~LinkListArenaAllocator()
		{
			for (void* nodes : m_arenas)
			{
				std::free(nodes);
			}
		}

		void* allocate() // new allocate() T;
		{
			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);
			while (tagPtr.getPointer() && !m_freeList.compare_exchange_weak(tagPtr, TPointer(tagPtr.getPointer()->next, tagPtr.getTag() + 1),
				std::memory_order_acq_rel, std::memory_order_relaxed))
			{
			}

			if (tagPtr.getPointer())
			{
				Node* ptr = tagPtr.getPointer();
				return reinterpret_cast<void*>(ptr);
			}

			// Block allocate when create new arena.
			std::lock_guard lock(m_arenaCreateMutex);
			if (!m_freeList.load(std::memory_order_acquire).getPointer())
			{
				createArena();
			}
			return allocate();
		}

		void free(T* tnode)
		{
			Node* node = reinterpret_cast<Node*>(tnode);

			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);
			node->next = tagPtr.getPointer();

			while (!m_freeList.compare_exchange_weak(tagPtr, TPointer(node, tagPtr.getTag() + 1),
				std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				node->next = tagPtr.getPointer();
			}
		}
	};

	// Small object just use heap allocator is fine.
	template<typename T>
	class HeapAllocator final : NonCopyable
	{
	public:
		T* allocate()
		{
			return reinterpret_cast<T*>(std::malloc(sizeof(T)));
		}

		inline void free(T* node)
		{
			std::free(reinterpret_cast<void*>(node));
		}
	};

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


	class PoolAllocator
	{
	public:
		explicit PoolAllocator(uint32_t elementSize)
			: m_elementSize(elementSize)
			, m_currentMaxSize(0)
		{

		}

		uint32_t allocate()
		{
			if (m_freeIds.empty())
			{
				return m_currentMaxSize ++;
			}

			uint32_t result = m_freeIds.top();
			m_freeIds.pop();
			return result;
		}

		void free(uint32_t id)
		{
			m_freeIds.push(id);
		}

		uint32_t getMaxSize() const
		{
			return m_currentMaxSize;
		}

	private:
		uint32_t m_currentMaxSize;
		uint32_t m_elementSize;
		std::stack<uint32_t> m_freeIds;
	};
}