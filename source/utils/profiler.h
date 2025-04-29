#pragma once

#include <utils/utils.h>

#define PROFILER_EVENT_NAME_REF  0
#define PROFILER_EVENT_NAME_COPY 1

#if CHORD_DEBUG
	#define PROFILER_EVENT_NAME PROFILER_EVENT_NAME_COPY
#else 
	#define PROFILER_EVENT_NAME PROFILER_EVENT_NAME_REF
#endif 

namespace chord
{
#if PROFILER_EVENT_NAME == PROFILER_EVENT_NAME_COPY
	using ProfilerNameType = std::string;
#elif PROFILER_EVENT_NAME == PROFILER_EVENT_NAME_REF
	using ProfilerNameType = const char*;
#endif 

	struct ProfilerEventCPU
	{
		ProfilerNameType name;

		std::chrono::time_point<std::chrono::system_clock> timeBegin;
		std::chrono::time_point<std::chrono::system_clock> timeEnd;

		uint32 threadId : 16; // Current event record in which thread.
		uint32 depth    : 16; // Current event depth.
	};
}