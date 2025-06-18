#include <utils/memory.h>
#include <tsl/robin_map.h>
#include <utils/profiler.h>
#include <utils/utils.h>

#if CHORD_DEBUG && defined(TRACY_ENABLE)
	#define TRACE_MEMORY 1
#else
	#define TRACE_MEMORY 0
#endif

void* chord::traceMalloc(std::size_t count)
{
	if (count == 0)
	{
		// avoid std::malloc(0) which may return nullptr on success
		++count;
	}

	if (void* ptr = std::malloc(count))
	{
		if constexpr (TRACE_MEMORY)
		{
			TracyAlloc(ptr, count);
			chord::GlobalMemoryStat::get().changeTraceMalloc(count);
		}
		return ptr;
	}
	throw std::bad_alloc{};
}

void chord::traceFree(void* ptr, std::size_t count)
{
	if constexpr (TRACE_MEMORY)
	{
		TracyFree(ptr);
		chord::GlobalMemoryStat::get().changeTraceMalloc(-count);
	}
	std::free(ptr);
}

chord::GlobalMemoryStat& chord::GlobalMemoryStat::get()
{
	static GlobalMemoryStat sInstance;
	return sInstance;
}

void* operator new(size_t count)
{
	if (count == 0)
	{
		// avoid std::malloc(0) which may return nullptr on success
		++count;
	}

	if (void* ptr = std::malloc(count))
	{
		if constexpr (TRACE_MEMORY)
		{
			TracyAlloc(ptr, count);
		}
		return ptr;
	}
	throw std::bad_alloc{};
}

void* operator new[](size_t count)
{
	if (count == 0)
	{
		// avoid std::malloc(0) which may return nullptr on success
		++count;
	}

	if (void* ptr = std::malloc(count))
	{
		if constexpr (TRACE_MEMORY)
		{
			TracyAlloc(ptr, count);
		}
		return ptr;
	}
	throw std::bad_alloc{};
}

void operator delete(void* ptr)
{
	if constexpr (TRACE_MEMORY)
	{
		TracyFree(ptr);
	}
	std::free(ptr);
}

void operator delete[](void* ptr)
{
	if constexpr (TRACE_MEMORY)
	{
		TracyFree(ptr);
	}
	std::free(ptr);
}
