#pragma once

#include <utils/utils.h>
#include <utils/work_stealing_queue.h>
#include <utils/intrusive_ptr.h>
#include <utils/profiler.h>
#include <utils/thread.h>

#define JOB_SYSTEM_DEBUG_NAME CHORD_DEBUG

namespace chord
{
	enum class EBusyWaitType : uint8
	{
		None,
		Foreground,
		All
	};

	enum class EJobFlags : uint8
	{
		None = 0x0,
		Foreground = 0x01 << 0,
		RunOnMainThread = 0x01 << 1,
	};
	ENUM_CLASS_FLAG_OPERATORS(EJobFlags);
}

namespace chord::jobsystem
{
	enum class EJobState : uint8
	{
		None = 0x0,

		// Job already create but still no push in queue.
		Pending,

		// Job already push in queue but still no execute.
		Pushed,

		// Job is executing.
		Executing,

		// Job finish.
		Finish,

		// Job is garbage now.
		Garbage,
	};

	struct alignas(kCpuCachelineSize) Job : NonCopyable
	{
		using JobFunction = void(*)(void* storage, Job&);
		void* storage[6];                        // 48 ... 48
		JobFunction function;                    // 8  ... 56
		uint16 dependencyArena;                  // 2 
		uint16 dependencyId;                     // 2  ... 60
		std::atomic<uint16> parentCounter;       // 2  ... 62
		std::atomic<EJobState> jobState;         // 1  ... 63
		EJobFlags flags;                         // 1  ... 64
	#if JOB_SYSTEM_DEBUG_NAME
		const char* debugName; // ... 8
		                       // ... 56 pad
	#endif 

		explicit Job(EJobFlags inFlags)
		{
			dependencyArena = uint16(~0U);
			dependencyId    = uint16(~0U);
			parentCounter   =  0;
			jobState        = EJobState::Pending;
			flags           = inFlags;
		}

		~Job();

		void* operator new(size_t size);
		void  operator delete(void* rawMemory);
	};

	struct JobChildLinkList : NonCopyable
	{
		JobChildLinkList* next = nullptr;
		Job* job = nullptr;

		void* operator new(size_t size);
		void  operator delete(void* rawMemory);
	};

	struct JobDependency : NonCopyable
	{
		// Current job is finish or not?
		std::atomic<bool> bFinish { false };

		//
		std::mutex mutex;

		// How many chilren depend on current job.
		JobChildLinkList* children = nullptr;

		void intrusive_ptr_counter_addRef()
		{
			m_counter++;
		}

		void intrusive_ptr_counter_release();

		void wait(EBusyWaitType waitType) const;

		void* operator new(size_t size);
		void  operator delete(void* rawMemory);

	private:
		std::atomic<uint32> m_counter{ 1 };
	};
	using JobDependencyRef = intrusive_ptr<JobDependency>;

	struct FutureCollection
	{
		std::vector<JobDependencyRef> futures;

		void wait(EBusyWaitType waitType)
		{
			for (auto& future : futures)
			{
				future->wait(waitType);
			}
		};

		void clear()
		{
			futures.clear();
		}

		bool isEmpty() const
		{
			return futures.empty();
		};

		float getProgress() const 
		{
			float progress = 0.0f;
			for (auto& future : futures)
			{
				progress += future->bFinish.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
			}
			return progress / float(futures.size());
		};

		void add(JobDependencyRef future)
		{
			futures.push_back(future);
		};

		void combine(FutureCollection ff)
		{
			futures.insert(futures.end(), ff.futures.begin(), ff.futures.end());
		};
	};

	extern void assignDependencyToJob(Job& job, JobDependency& dependency);
	extern void run(Job* job);

	// Interface...
	extern void init(int32 leftFreeCoreNum = 1);
	extern void waitAllJobFinish(EBusyWaitType waitType);
	extern void release(EBusyWaitType waitType);
	extern void busyWaitUntil(std::function<bool()>&& func, EBusyWaitType waitType);

	// 
	template<typename Lambda> // Lambda = [](){  }
	inline Job* createJob(EJobFlags flags, Lambda function)
	{
		static_assert(sizeof(Lambda) <= sizeof(Job::storage),
			"Don't pass over 48 char capture function input.");

		Job* job = new Job(flags);
		job->function = [](void* storage, Job& job)
		{
			Lambda* object = static_cast<Lambda*>(storage);
			object->operator()();
			object->~Lambda();
		};

		new (job->storage) Lambda(std::move(function));
		return job;
	}

	static inline void runJobWithDependency(Job* job, const std::vector<JobDependencyRef>& parents)
	{
		for (auto& parent : parents)
		{
			if (parent == nullptr) { continue; }
			parent->mutex.lock();
		}

		bool bExistParent = false;
		for (auto& parent : parents)
		{
			if (!parent || parent->bFinish)
			{
				continue;
			}

			JobChildLinkList* child = new JobChildLinkList();
			child->job = job;
			child->next = parent->children;
			parent->children = child;

			// New parent.
			job->parentCounter.fetch_add(1, std::memory_order_seq_cst);
			bExistParent = true;
		}

		for (auto& parent : parents)
		{
			if (parent == nullptr) { continue; }
			parent->mutex.unlock();
		}

		if (!bExistParent)
		{
			run(job);
		}
	}

	template<typename Lambda>
	inline void launchSilently(const char* debugName, EJobFlags flags, Lambda function, const std::vector<JobDependencyRef>& parents = {})
	{
		Job* job = createJob<Lambda>(flags, std::move(function));
	#if JOB_SYSTEM_DEBUG_NAME
		job->debugName = debugName;
	#endif 

		runJobWithDependency(job, parents);
	}

	template<typename Lambda>
	inline JobDependencyRef launch(const char* debugName, EJobFlags flags, Lambda function, const std::vector<JobDependencyRef>& parents = {})
	{
		Job* job = createJob<Lambda>(flags, std::move(function));
	#if JOB_SYSTEM_DEBUG_NAME
		job->debugName = debugName;
	#endif 
		JobDependencyRef eventRef = JobDependencyRef::create();
		eventRef->intrusive_ptr_counter_addRef();
		assignDependencyToJob(*job, *eventRef);
		runJobWithDependency(job, parents);
		return eventRef;
	}

	extern uint32 getUsableWorkerCount(bool bForeTask);

	// [taskContext](const uint32 loopStart, const uint32 loopEnd) {}
	using ParallelForFunc = std::function<void(const uint32 loopStart, const uint32 loopEnd)>;
	inline void parallelFor(const char* debugName, EBusyWaitType waitType, uint32 count, EJobFlags flags, 
		ParallelForFunc&& function)
	{
		const bool bForeTask = hasFlag(flags, EJobFlags::Foreground);

		// 
		uint32 perWorkerJobCount = divideRoundingUp(count, uint32(waitType != EBusyWaitType::None) + getUsableWorkerCount(bForeTask));

		uint32 dispatchTaskCount = 0;
		std::vector<JobDependencyRef> futures{};
		futures.reserve(perWorkerJobCount);

		struct LoopBody
		{
			ParallelForFunc func;
			const char* debugName;
		};

		LoopBody body { };
		body.debugName = debugName;
		body.func = std::move(function);

		while (dispatchTaskCount < count)
		{
			uint32 loopStart = dispatchTaskCount;
			uint32 loopEnd = std::min(loopStart + perWorkerJobCount, count);

			dispatchTaskCount += perWorkerJobCount;
			futures.push_back(launch(debugName, flags, [loopStart, loopEnd, &body]()
			{
				body.func(loopStart, loopEnd);
			}));
		}

		for (auto& future : futures)
		{
			future->wait(waitType);
		}
	}
}

namespace chord
{
	using JobDependencyRef = jobsystem::JobDependencyRef;
	using FutureCollection = jobsystem::FutureCollection;
}