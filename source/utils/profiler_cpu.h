#pragma once

#include <utils/utils.h>
#include <utils/string_table.h>

namespace chord::profiler_cpu
{
	struct ProfilerEventCPU
	{
		FName name;

		std::chrono::time_point<std::chrono::system_clock> timeBegin;
		std::chrono::time_point<std::chrono::system_clock> timeEnd;

		uint32 threadId : 16; // Current event record in which thread.
		uint32 depth    : 16; // Current event depth.
	};

	struct CacheProfilerEventCPU
	{
		FName name;

		// us -> 
		uint64 timeBeginPoint : 55; // us
		uint64 threadId : 9;
		uint64 timeSize : 48; // us
		uint64 depth : 16;
	};

	struct CacheProfilerEventCPURing
	{
		std::vector<CacheProfilerEventCPU> ringEvents;

		// Config
		uint64 growSize;
		uint64 maxSize;
		uint64 baseSize;

		// State
		uint64 ringSize  = 0;
		uint64 bufferPos = 0; // Iter bufferPos

		CacheProfilerEventCPURing()
			: growSize(0)
			, maxSize(0)
			, baseSize(0)
		{
		}

		CacheProfilerEventCPURing(uint64 inBaseSize, uint64 inGrowSize, uint64 inMaxSize)
			: growSize(inGrowSize)
			, maxSize(inMaxSize)
			, baseSize(inBaseSize)
			, bufferPos(0)
		{
			ringSize = baseSize;
			resize();
		}

		void resize()
		{
			ringEvents.resize(ringSize);
		}

		void push(CacheProfilerEventCPU&& event)
		{
			if (bufferPos + 1 >= ringSize && ringSize < maxSize)
			{
				ringSize *= growSize;
				resize();
			}

			//
			ringEvents.at((bufferPos + 1) % ringSize) = std::move(event);
			bufferPos ++;
		}

		void clear()
		{
			bufferPos = 0;
			ringSize  = baseSize;

			// 
			resize();
		}
	};

	struct PerThread
	{
		uint32 arrayIndex; // sAllThreads.perThreadData[arrayIndex]
		std::wstring_view threadName;

		std::vector<ProfilerEventCPU> events;
		std::stack<uint32> eventIdStack;

		// 
		std::atomic<bool> bPaused = false;
		CacheProfilerEventCPURing persistentCacheEvents;

		//
		void clean()
		{
			events = {};
			eventIdStack = {};
			bPaused = false;
			persistentCacheEvents.clear();
		}
	};



	// Get copy of all threads.
	extern std::vector<PerThread*>& getAllThreads();

	// Call these from main thread.
	extern void init();
	extern void cleanCache(); // clean all cache avoid used too much memory.
	extern void release();

	// Call from any thread.
	extern uint32 pushEvent(FName name);
	extern void popEvent(uint32 idx);

	struct ProfilerScopeObject : NonCopyable
	{
		uint32 idx;
		explicit ProfilerScopeObject(std::string_view name)
		{
			idx = pushEvent(name);
		}

		~ProfilerScopeObject()
		{
			popEvent(idx);
		}
	};
}

#define CPU_SCOPE_PROFILER(Name) chord::profiler_cpu::ProfilerScopeObject object_profiler_cpu(Name);