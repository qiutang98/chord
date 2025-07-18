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
	// Lock free allocator, memory not continually.
	template<typename T, int64_t kMaxCacheObject = std::numeric_limits<int64_t>::max()>
	class FreeListAllocator final : NonCopyable
	{
	public:
		using ValueType = T;

	private:
		struct Node
		{
			union
			{
				std::atomic<Node*> next{ nullptr };
				char data[sizeof(T)];
			};
		};
		static_assert(sizeof(Node) >= sizeof(T));

		using TPointer = TaggedPointer<Node>;
		static_assert(std::atomic<TPointer>::is_always_lock_free);

		alignas(kCpuCachelineSize) std::atomic<TPointer> m_freeList{ };
		alignas(kCpuCachelineSize) std::atomic<int64> m_freeCount{ 0 };
		alignas(kCpuCachelineSize) std::atomic<int64> m_allocatedCount{ 0 };


	public:
		explicit FreeListAllocator()
		{
		}

		~FreeListAllocator()
		{
			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);
			if (Node* node = tagPtr.getPointer())
			{
				Node* next;
				do
				{
					next = node->next.load(std::memory_order_relaxed);
					traceFree(reinterpret_cast<void*>(node), sizeof(Node));

					node = next;
					m_freeCount.fetch_sub(1, std::memory_order_relaxed);
					m_allocatedCount.fetch_sub(1, std::memory_order_relaxed);
				} while (node);
			}
			// Logic error if assert fail.
			assert(m_freeCount.load(std::memory_order_relaxed) == 0);

			// Must ensure all allocate one free before allocator destroy.
			assert(m_allocatedCount.load(std::memory_order_relaxed) == 0);
		}

		void* allocate() // new allocate() T;
		{
			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);
			while (tagPtr.getPointer() &&
				!m_freeList.compare_exchange_weak(tagPtr, TPointer(tagPtr.getPointer()->next.load(std::memory_order_acquire), tagPtr.getTag() + 1),
					std::memory_order_acq_rel,
					std::memory_order_relaxed))
			{
			}

			if (tagPtr.getPointer())
			{
				m_freeCount.fetch_sub(1, std::memory_order_acq_rel);

				Node* ptr = tagPtr.getPointer();
				return reinterpret_cast<void*>(ptr);
			}

			m_allocatedCount.fetch_add(1, std::memory_order_acq_rel);

			void* ptr = traceMalloc(sizeof(Node));
			return ptr;
		}

		void free(void* ptr) // ptr->~T(); free(ptr);
		{
			Node* node = reinterpret_cast<Node*>(ptr);
			if (m_freeCount.load(std::memory_order_relaxed) >= kMaxCacheObject)
			{
				traceFree(reinterpret_cast<void*>(node), sizeof(Node));

				m_allocatedCount.fetch_sub(1, std::memory_order_acq_rel);
				return;
			}

			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);
			node->next.store(tagPtr.getPointer(), std::memory_order_release);

			while (!m_freeList.compare_exchange_weak(tagPtr, TPointer(node, tagPtr.getTag() + 1),
				std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				node->next.store(tagPtr.getPointer(), std::memory_order_release);
			}

			m_freeCount.fetch_add(1, std::memory_order_acq_rel);
		}
	};

	// Fixed size arena allocator, never grow.
	template<typename T, size_t kPageSize>
	class FreeListFixedArenaAllocator final : NonCopyable
	{
		struct Node
		{
			union
			{
				std::atomic<Node*> next{ nullptr };
				char data[sizeof(T)];
			};
		};
		using TPointer = TaggedPointer<Node>;
		static_assert(sizeof(Node) >= sizeof(T));

		//
		constexpr static int64 kCapacity = kPageSize / sizeof(Node);

		Node* m_nodeArray = nullptr;
		void* m_arena = nullptr;

		alignas(kCpuCachelineSize) std::atomic<int64> m_freeCounter;
		alignas(kCpuCachelineSize) std::atomic<TPointer> m_freeList{ };

	public:
		explicit FreeListFixedArenaAllocator()
		{
			m_arena = traceMalloc(kPageSize);
			m_freeCounter.store(kCapacity, std::memory_order_relaxed);

			// 
			m_nodeArray = reinterpret_cast<Node*>(m_arena);
			Node* head = &m_nodeArray[0];
			Node* current = head;

			// Prepare freelist.
			for (uint32 i = 1U; i < kCapacity; i++)
			{
				Node* next = &m_nodeArray[i];
				current->next.store(next, std::memory_order_relaxed);
				current = next;
			}
			current->next.store(nullptr, std::memory_order_relaxed); // Last element update next.

			// Prepare freelist node.
			m_freeList.store(TPointer(head, 0), std::memory_order_relaxed);

			// Barrier here.
			std::atomic_thread_fence(std::memory_order_release);
		}

		~FreeListFixedArenaAllocator()
		{
			traceFree(m_arena, kPageSize);
		}

		//
		int64 computeAndCheckOffset(void* ptr)
		{
			Node* castType = reinterpret_cast<Node*>(ptr);
			if constexpr (CHORD_DEBUG)
			{
				ptrdiff_t ptrDiff = (castType - m_nodeArray);
				assert(ptrDiff >= 0 && ptrDiff < kCapacity);
			}
			return int64(castType - m_nodeArray);
		}

		//
		T* get(int64 index) const
		{
			assert(index < kCapacity);
			return reinterpret_cast<T*>(&m_nodeArray[index]);
		}

		void* allocate() // new allocate() T;
		{
			auto oldFreeCount = m_freeCounter.fetch_sub(1, std::memory_order_acq_rel);
			assert(oldFreeCount >= 1);

			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);
			while (!m_freeList.compare_exchange_weak(tagPtr, TPointer(tagPtr.getPointer()->next.load(std::memory_order_acquire), tagPtr.getTag() + 1),
				std::memory_order_seq_cst, std::memory_order_relaxed))
			{
			}
			return reinterpret_cast<void*>(tagPtr.getPointer());
		}

		void free(void* ptr) // ptr->~T(); free(ptr);
		{
			Node* node = reinterpret_cast<Node*>(ptr);

			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);
			node->next.store(tagPtr.getPointer(), std::memory_order_release);

			while (!m_freeList.compare_exchange_weak(tagPtr, TPointer(node, tagPtr.getTag() + 1),
				std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				node->next.store(tagPtr.getPointer(), std::memory_order_release);
			}

			m_freeCounter.fetch_add(1, std::memory_order_acq_rel);
		}
	};

	// 'Lock free' (Lock when arena allocate) allocator, arena memory continually.
	template<typename T, size_t kPageSize, size_t kArenaMaxCount = std::numeric_limits<size_t>::max()>
	class FreeListArenaAllocator final : NonCopyable
	{
	public:
		using ValueType = T;

	private:
		struct Node
		{
			union
			{
				std::atomic<Node*> next{ nullptr };
				char data[sizeof(T)];
			};
		};
		static_assert(sizeof(Node) >= sizeof(T));

		using TPointer = TaggedPointer<Node>;
		static_assert(sizeof(Node) < kPageSize);
		static constexpr uint32 kElementCount = kPageSize / sizeof(Node);

		alignas(kCpuCachelineSize) std::atomic<TPointer> m_freeList{ };
		static_assert(std::atomic<TPointer>::is_always_lock_free);

		std::shared_mutex m_arenaCreateMutex;
		std::vector<void*> m_arenas{ };

		void createArena()
		{
			assert(m_arenas.size() < kArenaMaxCount);

			void* memory = traceMalloc(kPageSize);
			m_arenas.push_back(memory);

			Node* newBlob = reinterpret_cast<Node*>(memory);
			for (uint32 i = 0; i < kElementCount; i++)
			{
				this->free(reinterpret_cast<void*>(&newBlob[i]));
			}
		}

	public:
		explicit FreeListArenaAllocator(bool bCreateArenaWhenInit = true)
		{
			if (bCreateArenaWhenInit)
			{
				createArena();
			}
		}

		~FreeListArenaAllocator()
		{
			for (void* arena : m_arenas)
			{
				traceFree(arena, kPageSize);
			}
		}

		void* getPtr(int32 arenaIndex, int32 index)
		{
			void* arena = m_arenas.at(arenaIndex);
			Node* blob = reinterpret_cast<Node*>(arena);
			assert(index < kElementCount);
			return reinterpret_cast<void*>(&blob[index]);
		}

		// WARNING: O(n) complexity to find index, so if need current function to find the index.
		//          You should make page larger, so arena count smaller make this function faster.
		inline std::pair<int32, int32> computeArenaIndexAndOffset(void* ptr)
		{
			Node* node = reinterpret_cast<Node*>(ptr);
			{
				std::shared_lock lock(m_arenaCreateMutex);
				for (int32 arenaIndex = 0; arenaIndex < m_arenas.size(); arenaIndex++)
				{
					Node* arenaPtr = reinterpret_cast<Node*>(m_arenas[arenaIndex]);
					if (node - arenaPtr < kElementCount)
					{
						return { arenaIndex, int32(node - arenaPtr) };
					}
				}
			}

			return { -1, -1 };
		}

		void* allocate() // new allocate() T;
		{
			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);

			// memory_order_seq_cst for next atomic store can flush before compare.
			while (tagPtr.getPointer() &&
				!m_freeList.compare_exchange_weak(tagPtr, TPointer(tagPtr.getPointer()->next.load(std::memory_order_acquire), tagPtr.getTag() + 1),
					std::memory_order_seq_cst, std::memory_order_relaxed))
			{
			}

			if (tagPtr.getPointer())
			{
				Node* ptr = tagPtr.getPointer();
				return reinterpret_cast<void*>(ptr);
			}

			// Block allocate when create new arena.
			std::unique_lock lock(m_arenaCreateMutex);
			if (!m_freeList.load(std::memory_order_acquire).getPointer())
			{
				createArena();
			}
			return allocate();
		}

		void free(void* ptr) // ptr->~T(); free(ptr);
		{
			Node* node = reinterpret_cast<Node*>(ptr);

			TPointer tagPtr = m_freeList.load(std::memory_order_relaxed);
			node->next.store(tagPtr.getPointer(), std::memory_order_release);

			// memory_order_seq_cst for next atomic store can flush before compare.
			while (!m_freeList.compare_exchange_weak(tagPtr, TPointer(node, tagPtr.getTag() + 1),
				std::memory_order_seq_cst, std::memory_order_relaxed))
			{
				node->next.store(tagPtr.getPointer(), std::memory_order_release);
			}
		}
	};

	// Small object just use heap allocator is fine.
	template<typename T>
	class HeapAllocator final : NonCopyable
	{
		alignas(kCpuCachelineSize) std::atomic<int64> m_counter{ 0 };
	public:
		using ValueType = T;

		~HeapAllocator()
		{
			assert(m_counter.load(std::memory_order_relaxed) == 0);
		}

		inline void* allocate() // new allocate() T;
		{
			m_counter.fetch_add(1, std::memory_order_acq_rel);
			return reinterpret_cast<T*>(traceMalloc(sizeof(T)));
		}

		inline void free(T* node) // ptr->~T(); free(ptr);
		{
			traceFree(reinterpret_cast<void*>(node), sizeof(T));
			m_counter.fetch_sub(1, std::memory_order_acq_rel);
		}
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
				return m_currentMaxSize++;
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