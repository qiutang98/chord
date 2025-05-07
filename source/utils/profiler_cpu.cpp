#include <utils/profiler_cpu.h>
#include <utils/thread.h>

namespace chord::profiler_cpu
{
	constexpr uint64 kMaxRecordEventRing      = (1 << 20u); // Max 24 MB per thread
	constexpr uint64 kBaseRecordEventRing     = (1 <<  8u);
	constexpr uint64 kRecordEventRingGrowSize = (1 <<  2u);

	static std::chrono::time_point<std::chrono::system_clock> sApplicationBeginTime{ };

	static thread_local std::unique_ptr<PerThread> tlsPerThreadData = nullptr;
	struct
	{
		std::mutex mutex;
		std::vector<PerThread*> perThreadData;
	} sAllThreads;

	std::vector<PerThread*>& chord::profiler_cpu::getAllThreads()
	{
		return sAllThreads.perThreadData; // Don't care multi thread here?
	}

	static PerThread* getOrCreateTLSIfNoExist()
	{
		if (tlsPerThreadData == nullptr)
		{
			tlsPerThreadData = std::make_unique<PerThread>();
			{
				std::lock_guard lock(sAllThreads.mutex);

				tlsPerThreadData->arrayIndex = sAllThreads.perThreadData.size();
				tlsPerThreadData->threadName = getCurrentThreadName();
				tlsPerThreadData->persistentCacheEvents = CacheProfilerEventCPURing(
					kBaseRecordEventRing, kRecordEventRingGrowSize, kMaxRecordEventRing);

				sAllThreads.perThreadData.push_back(tlsPerThreadData.get());
			}
		}

		return tlsPerThreadData.get();
	}

	static void pausedSTAT()
	{
		std::lock_guard lock(sAllThreads.mutex);
		for (auto* tls : sAllThreads.perThreadData)
		{
			tls->bPaused = true;
		}
	}

	void profiler_cpu::cleanCache()
	{
		assert(isInMainThread());
		for (auto* tls : sAllThreads.perThreadData)
		{
			tls->persistentCacheEvents.clear();
		}
	}

	void profiler_cpu::init()
	{
		sApplicationBeginTime = std::chrono::system_clock::now();
	}

	void profiler_cpu::release()
	{
		std::lock_guard lock(sAllThreads.mutex);
		for (auto* tls : sAllThreads.perThreadData)
		{
			tls->clean();
		}
	}

	uint32 profiler_cpu::pushEvent(FName name)
	{
		PerThread* tls = getOrCreateTLSIfNoExist();
		if (tls->bPaused)
		{
			return ~0U;
		}

		ProfilerEventCPU newEvent { };
		newEvent.name      = name;
		newEvent.timeBegin = std::chrono::system_clock::now();
		newEvent.depth     = tls->eventIdStack.size();
		newEvent.threadId  = tls->arrayIndex;

		uint32 result = tls->events.size();
		tls->eventIdStack.push(result);
		tls->events.push_back(std::move(newEvent));

		return result;
	}

	void profiler_cpu::popEvent(uint32 idx)
	{
		if (idx == ~0U)
		{
			return; // Skip invalid idx.
		}

		PerThread* tls = getOrCreateTLSIfNoExist();
		uint32 id = tls->eventIdStack.top();
		assert(idx == id);
		tls->eventIdStack.pop();
		auto& event = tls->events.at(id);
		event.timeEnd = std::chrono::system_clock::now();

		// All task finish, send to buffer.
		if (tls->eventIdStack.size() == 0)
		{
			std::vector<CacheProfilerEventCPU> submitCache{};
			submitCache.reserve(tls->events.size());
			for (const auto& event : tls->events)
			{
				CacheProfilerEventCPU submitEvent{};
				submitEvent.name = event.name;
				submitEvent.depth = event.depth;
				submitEvent.threadId = event.threadId;
				submitEvent.timeBeginPoint = std::chrono::duration_cast<std::chrono::microseconds>(event.timeBegin - sApplicationBeginTime).count();
				submitEvent.timeSize = std::chrono::duration_cast<std::chrono::microseconds>(event.timeEnd - event.timeBegin).count();
				
				// Already sorted by time begin point natively.
				submitCache.push_back(submitEvent);
			}

			// Reset temp events.
			tls->events.clear(); 

			// Send to main queue.
			ENQUEUE_MAIN_COMMAND([submitCache = std::move(submitCache), tls]() mutable
			{
				for(auto& e : submitCache)
				{
					tls->persistentCacheEvents.push(std::move(e));
				}
			});
		}
	}
}