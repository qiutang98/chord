#include <utils/profiler.h>

namespace chord::profiler_cpu
{
	struct PerThread
	{
		uint32 arrayIndex; // sAllThreads.perThreadData[arrayIndex]

		std::vector<ProfilerEventCPU> events;
		std::stack<uint32> eventIdStack;

		std::atomic<bool> bPaused = false;

		void clean()
		{
			events = {};
			eventIdStack = {};
			bPaused = false;
		}
	};

	static thread_local std::unique_ptr<PerThread> tlsPerThreadData = nullptr;
	struct
	{
		std::mutex mutex;
		std::vector<PerThread*> perThreadData;
	} sAllThreads;


	static PerThread* getOrCreateTLSIfNoExist()
	{
		if (tlsPerThreadData != nullptr)
		{
			return tlsPerThreadData.get();
		}

		tlsPerThreadData = std::make_unique<PerThread>();
		{
			std::lock_guard lock(sAllThreads.mutex);

			tlsPerThreadData->arrayIndex = sAllThreads.perThreadData.size();
			sAllThreads.perThreadData.push_back(tlsPerThreadData.get());
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

	static void release()
	{
		std::lock_guard lock(sAllThreads.mutex);
		for (auto* tls : sAllThreads.perThreadData)
		{
			tls->clean();
		}
	}

	static uint32 pushEvent(ProfilerNameType& name)
	{
		PerThread* tls = getOrCreateTLSIfNoExist();
		if (tls->bPaused)
		{
			return ~0U;
		}

		ProfilerEventCPU newEvent { };
		if constexpr (std::is_same_v<ProfilerNameType, const char*>)
		{
			newEvent.name = name;
		}
		else
		{
			newEvent.name = std::move(name);
		}
		
		newEvent.timeBegin = std::chrono::system_clock::now();
		newEvent.depth     = tls->eventIdStack.size();
		newEvent.threadId  = tls->arrayIndex;

		tls->eventIdStack.push(tls->events.size());
		tls->events.push_back(std::move(newEvent));
	}

	static void popEvent(uint32 idx)
	{
		if (idx == ~0U)
		{
			return;
		}

		PerThread* tls = getOrCreateTLSIfNoExist();
		uint32 id = tls->eventIdStack.top();
		assert(idx == id);
		tls->eventIdStack.pop();
		auto& event = tls->events.at(id);
		event.timeEnd = std::chrono::system_clock::now();

		// All task finish, send to counter.
		if (tls->eventIdStack.size() == 0)
		{

		}
	}
}