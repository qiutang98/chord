#pragma once

#include <tsl/robin_map.h>
#include <shared_mutex>

namespace chord
{
	extern void* traceMalloc(std::size_t count);
	extern void traceFree(void* ptr, std::size_t count);

	class GlobalMemoryStat
	{
	private:
		GlobalMemoryStat() = default;
		tsl::robin_map<const char*, int64_t> m_memoryStatMap;
		std::atomic<int64_t> m_traceMalloc = 0;

	public:
		static GlobalMemoryStat& get();
		using Key = int64_t*;

		void changeTraceMalloc(int64_t size)
		{
			m_traceMalloc.fetch_add(size);
		}

		Key registerStat(const char* name)
		{
			m_memoryStatMap[name] = 0;
			return &m_memoryStatMap[name];
		}

		const auto& getStatMapView() const
		{
			return m_memoryStatMap;
		}

		void changeMemoryStat(Key& name, int64_t size)
		{
			std::atomic_thread_fence(std::memory_order_acquire);
			(*name) += size;
			std::atomic_thread_fence(std::memory_order_release);
		}
	};
}

#define MEMORY_STAT_DEFINE(Key, name) static auto* Key = chord::GlobalMemoryStat::get().registerStat(name);

#define MEMORY_STAT_ADD(Key, size) chord::GlobalMemoryStat::get().changeMemoryStat(Key,  size); 
#define MEMORY_STAT_SUB(Key, size) chord::GlobalMemoryStat::get().changeMemoryStat(Key, -size); 


void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* memory);
void operator delete[](void* memory);