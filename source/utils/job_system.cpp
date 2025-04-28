#include <utils/job_system.h>
#include <utils/log.h>
#include <utils/mpmc_queue.h>
#include <utils/tagged_ptr.h>
#include <utils/allocator.h>
#include <utils/thread.h>

namespace chord::jobsystem
{
	constexpr int64 kMaxJobCount = 1ll << 14ll;
	constexpr int32 kGlobalQueueForeTaskIndex = 0;
	constexpr int32 kGlobalQueueAnyTaskIndex  = 1;

	// 
	using WorkQueue = WorkStealingQueue<uint16>;
	using GlobalQueueType = MPMCQueue<uint16>;

	// Read only config
	struct JobSystemConfig
	{
		// Current job system can work effective?
		bool bEffective = false;

		// 
		uint32 totalWorkerNum = 0;
		uint32 foregroundWorkerNum = 0;

		std::vector<std::future<void>> workerFutures { };
		std::vector<WorkQueue*> workQueues;

		std::array<std::unique_ptr<GlobalQueueType>, 2> globalWorkQueues = { nullptr, nullptr };
	};
	static JobSystemConfig sConfig { };

	// Global job system allocator.
	// 1 MB.
	using JobAllocator = FreeListFixedArenaAllocator<Job, sizeof(Job)* kMaxJobCount>;
	static alignas(kCpuCachelineSize) std::atomic<JobAllocator*> sJobAllocator { nullptr };

	// Max 16 times to indexing dependency.
	// 256 kb per page can store 2730 element, max need 7 page.
	using JobDependencyAllocator = FreeListArenaAllocator<JobDependency, 256 * 1024>;
	static alignas(kCpuCachelineSize) std::atomic<JobDependencyAllocator*> sJobDependencyAllocator { nullptr };

	// 16 kb per page can store 1024 element.
	using JobChildLinkListAllocator = FreeListArenaAllocator<JobChildLinkList, 16 * 1024>;
	static alignas(kCpuCachelineSize) std::atomic<JobChildLinkListAllocator*> sJobChildLinkListAllocator { nullptr };

	// 
	static alignas(kCpuCachelineSize) std::atomic<bool> sRuning{ false };

	// 
	static alignas(kCpuCachelineSize) std::atomic<int32> sQueuedForeJobCount { 0 };
	static alignas(kCpuCachelineSize) std::atomic<int32> sQueuedAnyJobCount { 0 };

	struct ConditionalVariable
	{
		std::condition_variable cv;
		std::mutex mutex;
	};
	static alignas(kCpuCachelineSize) ConditionalVariable sRecentQueueForeJob { };
	static alignas(kCpuCachelineSize) ConditionalVariable sRecentQueueAnyJob  { };

	// Worker only.
	struct alignas(kCpuCachelineSize) PerThreadLocalData
	{
		uint32 minWorkerIndex; // 
		uint32 maxWorkerIndex; // 
		uint32 seed;

		// 
		std::unique_ptr<WorkQueue> queue = nullptr;

		// 
		int32 threadIndex = -1;

		// Current worker is foreground worker or not.
		bool bForegroundWorker = false;

		// More than one thread for current type worker to stole.
		bool bStoleMoreThanOneThread = false;

		static inline uint32 pacgHash(uint32& inOutState, uint32 inMinWorkerIndex, uint32 inMaxWorkerIndex)
		{
			inOutState = inOutState * 747796405u + 2891336453u;
			uint32 word = ((inOutState >> ((inOutState >> 28u) + 4u)) ^ inOutState) * 277803737u;
			return ((word >> 22u) ^ word) % (1 + inMaxWorkerIndex - inMinWorkerIndex) + inMinWorkerIndex;
		}

		// PCG hash.
		inline uint32 rand()
		{
			return pacgHash(seed, minWorkerIndex, maxWorkerIndex);
		}
	};
	static thread_local std::unique_ptr<PerThreadLocalData> tlsWorkerData = nullptr;

	static inline bool isJobInQueues(bool bForeJob)
	{
		return bForeJob 
			? sQueuedForeJobCount.load(std::memory_order_acquire) > 0
			: sQueuedAnyJobCount.load(std::memory_order_acquire)  > 0;
	}

	static inline bool isJobSystemRequiredStop()
	{
		return !sRuning.load(std::memory_order_relaxed);
	}

	void JobDependency::intrusive_ptr_counter_release()
	{
		if (auto* allocator = sJobDependencyAllocator.load(std::memory_order_acquire))
		{
			uint32 result = --m_counter;
			if (result == 0)
			{
				delete this;
			}
		}
	}

	void JobDependency::operator delete(void* rawMemory)
	{
		if (auto* allocator = sJobDependencyAllocator.load(std::memory_order_acquire))
		{
			allocator->free(rawMemory);
		}
	}

	void* JobDependency::operator new(size_t size)
	{
		auto* allocator = sJobDependencyAllocator.load(std::memory_order_acquire);
		check(allocator && size == sizeof(JobDependency));
		return allocator->allocate();
	}

	void* JobChildLinkList::operator new(size_t size)
	{
		auto* allocator = sJobChildLinkListAllocator.load(std::memory_order_acquire);
		check(allocator && size == sizeof(JobChildLinkList));
		return allocator->allocate();
	}

	void JobChildLinkList::operator delete(void* rawMemory)
	{
		if (auto* allocator = sJobChildLinkListAllocator.load(std::memory_order_acquire))
		{
			allocator->free(rawMemory);
		}
	}

	void* Job::operator new(size_t size)
	{
		auto* allocator = sJobAllocator.load(std::memory_order_acquire);
		check(allocator && size == sizeof(Job));
		return allocator->allocate();
	}

	Job::~Job()
	{
		check(jobState == EJobState::Garbage);
	}
	void Job::operator delete(void* rawMemory)
	{
		if (auto* allocator = sJobAllocator.load(std::memory_order_acquire))
		{
			allocator->free(rawMemory);
		}
	}

	static void execute(Job* job);

	template<class EnqueueFunction>
	static void pushToQueue(Job* job, EnqueueFunction&& func)
	{
		check(job->jobState == EJobState::Pending);
		if (auto* allocator = sJobAllocator.load(std::memory_order_acquire))
		{
			//
			const uint16 jobIndex = allocator->computeAndCheckOffset(job);
			job->jobState = EJobState::Pushed;

			func(jobIndex);

			sQueuedAnyJobCount.fetch_add(1, std::memory_order_seq_cst);
			sRecentQueueAnyJob.cv.notify_one();

			if (hasFlag(job->flags, EJobFlags::Foreground))
			{
				sQueuedForeJobCount.fetch_add(1, std::memory_order_seq_cst);
				sRecentQueueForeJob.cv.notify_one();
			}
		}
		else
		{
			busyWaitUntil([job]() { return job->parentCounter.load() == 0; }, EBusyWaitType::All);
			execute(job);
		}
	}

	static void pushToQueue(Job* job)
	{
		check(job->jobState == EJobState::Pending);
		const bool bForegroundJob = hasFlag(job->flags, EJobFlags::Foreground);

		// Try push to thread local queue if exist.
		if (tlsWorkerData)
		{
			bool bCanPushToLocalQueue = false;
			if (bForegroundJob)
			{
				// Foreground job only push to foreground queue.
				// NOTE: foreground worker only steal foreground queue job.
				bCanPushToLocalQueue = tlsWorkerData->bForegroundWorker;
			}
			else
			{
				// Background job only push to background queue.
				// NOTE: background worker can steal foreground queue job.
				bCanPushToLocalQueue = !tlsWorkerData->bForegroundWorker;
			}

			if (bCanPushToLocalQueue)
			{
				pushToQueue(job, [](uint16 jobIndex) { tlsWorkerData->queue->push(jobIndex);});
				return;
			}
		}

		// Can't push to local queue, then push to global queue.
		uint32 globalQueueIndex = bForegroundJob ? kGlobalQueueForeTaskIndex : kGlobalQueueAnyTaskIndex;
		{
			pushToQueue(job, [globalQueueIndex](uint16 jobIndex) { sConfig.globalWorkQueues[globalQueueIndex]->enqueue(jobIndex); });
		}
	}



	void assignDependencyToJob(Job& job, JobDependency& dependency)
	{
		check(job.jobState == EJobState::Pending);
		check(dependency.bFinish.load(std::memory_order_seq_cst) == false);

		auto* allocator = sJobDependencyAllocator.load(std::memory_order_acquire);
		auto arenaIndex = allocator->computeArenaIndexAndOffset(&dependency);

		job.dependencyArena = arenaIndex.first;
		job.dependencyId = arenaIndex.second;
		check(job.dependencyArena >= 0 && job.dependencyId >= 0);
	}

	static void execute(Job* job)
	{
		if constexpr (CHORD_DEBUG)
		{
			check(job->jobState.load(std::memory_order_seq_cst) == EJobState::Pushed);
			check(job->parentCounter.load(std::memory_order_seq_cst) == 0);
		}

		job->jobState.store(EJobState::Executing, std::memory_order_seq_cst);
		{
			job->function(job->storage, *job);
		}
		job->jobState.store(EJobState::Finish, std::memory_order_seq_cst);

		if (job->dependencyArena != uint16(~0U) && job->dependencyId != uint16(~0U))
		{
			auto* allocator = sJobDependencyAllocator.load(std::memory_order_relaxed);
			JobDependency* dependency = (JobDependency*)allocator->getPtr(job->dependencyArena, job->dependencyId);
			{
				std::lock_guard lock(dependency->mutex);

				dependency->bFinish.store(true, std::memory_order_seq_cst);
				JobChildLinkList* child = dependency->children;

				while (child)
				{
					Job* job = child->job;
					uint16 oldDependencyCount = job->parentCounter.fetch_sub(1, std::memory_order_seq_cst);
					check(oldDependencyCount > 0);

					// Current job already finish all dependency job, it's time to enqueue. 
					if (oldDependencyCount == 1)
					{
						run(job);
					}

					// Iterate next child.
					auto* garbage = child;
					child = child->next;

					// Free link list garbage.
					delete garbage;
				}
			}
			dependency->intrusive_ptr_counter_release();
		}

		// Now current job can destroy.
		job->jobState.store(EJobState::Garbage, std::memory_order_seq_cst);
		delete job; // Job finish and collect it.
	}

	void chord::jobsystem::run(Job* job)
	{
		if (hasFlag(job->flags, EJobFlags::RunOnMainThread))
		{
			// Current job require run on main thread.
			check(job->jobState == EJobState::Pending);
			job->jobState = EJobState::Pushed;
			ENQUEUE_MAIN_COMMAND([job]() { execute(job); });
		}
		else
		{
			pushToQueue(job);
		}
	}

	static void reduceJobQueueCount(Job* job)
	{
		sQueuedAnyJobCount.fetch_sub(1, std::memory_order_seq_cst);
		if (hasFlag(job->flags, EJobFlags::Foreground))
		{
			sQueuedForeJobCount.fetch_sub(1, std::memory_order_seq_cst);
		}
	}

	static Job* findOneJobFromGlobal(bool bIncludedAnyJob)
	{
		uint16 jobIndex;
		bool bSuccess = sConfig.globalWorkQueues[kGlobalQueueForeTaskIndex]->dequeue(jobIndex);

		if (!bSuccess && bIncludedAnyJob)
		{
			bSuccess = sConfig.globalWorkQueues[kGlobalQueueAnyTaskIndex]->dequeue(jobIndex);
		}

		if (bSuccess)
		{
			Job* job = sJobAllocator.load(std::memory_order_relaxed)->get(jobIndex);
			reduceJobQueueCount(job);
			return job;
		}

		std::this_thread::yield();
		return nullptr;
	}

	// 
	static Job* findOneJobFromLocalQueue(bool bIncludedAnyJob)
	{
		std::optional<uint16> jobIndex;

		// Try pop from local worker.
		if (tlsWorkerData) 
		{
			PerThreadLocalData& tls = *tlsWorkerData;

			// Try pop first.
			WorkQueue* queue = tls.queue.get();
			jobIndex = queue->pop();
			if (jobIndex.has_value())
			{
				// Already found one job
			}
			else
			{
				if (!tls.bStoleMoreThanOneThread)
				{
					// Current worker type only one thread so just return.
					// Don't steal.
					std::this_thread::yield();
					return nullptr;
				}
			}
		}

		// Try stole form other worker.
		if (!jobIndex.has_value())
		{
			int32 randomId;
			if (tlsWorkerData)
			{
				PerThreadLocalData& tls = *tlsWorkerData;
				randomId = tls.rand();
				if (randomId == tls.threadIndex)
				{
					// Hit same thread don't execute it.
					std::this_thread::yield();
					return nullptr;
				}
			}
			else
			{
				// thread local rnd gen.
				static thread_local uint32 seed = ::clock();

				const uint32 cStoleWorkerNum = bIncludedAnyJob ? sConfig.totalWorkerNum : sConfig.foregroundWorkerNum;
				randomId = PerThreadLocalData::pacgHash(seed, 0, cStoleWorkerNum - 1);
			}

			WorkQueue* rndQueue = sConfig.workQueues.at(randomId);
			jobIndex = rndQueue->steal();
		}

		if (jobIndex.has_value())
		{
			Job* job = sJobAllocator.load(std::memory_order_relaxed)->get(jobIndex.value());
			reduceJobQueueCount(job);
			return job;
		}

		std::this_thread::yield();
		return nullptr;
	}

	static Job* findOneJob(bool bIncludedAnyJob)
	{
		Job* job = findOneJobFromLocalQueue(bIncludedAnyJob);
		if (job == nullptr)
		{
			job = findOneJobFromGlobal(bIncludedAnyJob);
		}

		return job;
	}

	static void workerLoop(
		const std::wstring& name,
		const int32 workerIndex, 
		const int32 foregroundWorkerNum,
		const int32 backgroundWorkerNum,
		std::atomic<int32>& workerInitCounter)
	{
		const int32 totalWorkerNum = foregroundWorkerNum + backgroundWorkerNum;
		const bool bForegroundWorker = workerIndex < foregroundWorkerNum;

		chord::namedCurrentThread(name);

		assert(sConfig.bEffective);
		assert(workerIndex < totalWorkerNum);

		// Foreground worker only stole job from foreground thread.
		// Background worker can stole from anywhere.
		const int32 cStoleWorkerNum = bForegroundWorker ? foregroundWorkerNum : totalWorkerNum;
		check(tlsWorkerData == nullptr);
		tlsWorkerData = std::make_unique<PerThreadLocalData>();

		//
		tlsWorkerData->threadIndex = workerIndex;
		tlsWorkerData->queue = std::make_unique<WorkQueue>(1 << 14U);
		tlsWorkerData->bForegroundWorker = bForegroundWorker;
		tlsWorkerData->minWorkerIndex = 0;
		tlsWorkerData->maxWorkerIndex = cStoleWorkerNum - 1;
		tlsWorkerData->bStoleMoreThanOneThread = (cStoleWorkerNum > 1);
		tlsWorkerData->seed = workerIndex;
		// Fill global queue.
		sConfig.workQueues[workerIndex] = tlsWorkerData->queue.get();

		// Init finished.
		std::atomic_thread_fence(std::memory_order_seq_cst);
		workerInitCounter.fetch_add(1);

		//
		auto& waitAtomicSignal = bForegroundWorker ? sRecentQueueForeJob : sRecentQueueAnyJob;
		do
		{
			bool bSuccess = false;
			if (isJobInQueues(bForegroundWorker))
			{
				Job* job = findOneJob(!tlsWorkerData->bForegroundWorker);
				if (job)
				{
					execute(job);
					bSuccess = true;
				}
			}

			if (!bSuccess)
			{
				std::unique_lock lock(waitAtomicSignal.mutex);
				while (!isJobInQueues(bForegroundWorker) && !isJobSystemRequiredStop())
				{
					waitAtomicSignal.cv.wait(lock);
				}
			}
		} while (!isJobSystemRequiredStop());

		// Clean thread resource.
		tlsWorkerData = nullptr;
	}

	static inline void busyWait(EBusyWaitType waitType)
	{
		if (waitType == EBusyWaitType::None)
		{
			std::this_thread::yield();
			return;
		}

		Job* job = nullptr;

		if (waitType == EBusyWaitType::Foreground || waitType == EBusyWaitType::All)
		{
			job = findOneJob(false);
		}

		if (waitType == EBusyWaitType::All)
		{
			if (job == nullptr)
			{
				job = findOneJob(true);
			}
		}

		if (job)
		{
			execute(job);
		}
		else
		{
			std::this_thread::yield();
		}
	}

	void JobDependency::wait(EBusyWaitType waitType) const
	{
		while (!bFinish.load(std::memory_order_relaxed))
		{
			busyWait(waitType);
		}
	}

	void chord::jobsystem::busyWaitUntil(std::function<bool()>&& func, EBusyWaitType waitType)
	{
		while (!func())
		{
			busyWait(waitType);
		}
	}

	uint32 getUsableWorkerCount(bool bForeTask)
	{
		return bForeTask ? sConfig.totalWorkerNum : sConfig.totalWorkerNum - sConfig.foregroundWorkerNum;
	}

	void waitAllJobFinish(EBusyWaitType waitType)
	{
		while (isJobInQueues(false) || isJobInQueues(true))
		{
			busyWait(waitType);
		}
	}

	void release(EBusyWaitType waitType)
	{
		if (waitType != EBusyWaitType::None)
		{
			waitAllJobFinish(waitType);
		}

		sRuning.store(false, std::memory_order_seq_cst);
		sRecentQueueForeJob.cv.notify_all();
		sRecentQueueAnyJob.cv.notify_all();

		for (auto& f : sConfig.workerFutures)
		{
			f.wait();
		}

		sQueuedAnyJobCount  = 0;
		sQueuedForeJobCount = 0;

		auto releaseAllocator = [](auto& atomicAllocator)
		{
			if (auto* allocator = atomicAllocator.load(std::memory_order_relaxed))
			{
				delete allocator;
			}
			atomicAllocator.store(nullptr, std::memory_order_release);
		};
		releaseAllocator(sJobDependencyAllocator);
		releaseAllocator(sJobAllocator);
		releaseAllocator(sJobChildLinkListAllocator);

		sConfig = {};
	}

	void jobsystem::init(int32 leftFreeCoreNum)
	{
		const int32 kMaxCoreThreadNum = std::thread::hardware_concurrency();
		const int32 kDesiredWorkerNum = kMaxCoreThreadNum - leftFreeCoreNum;
		if (kDesiredWorkerNum <= 0)
		{
			release(EBusyWaitType::None);

			LOG_WARN("No more free core to create thread, Jobsystem will run in one thread mode.");
			return;
		}

		// 
		sConfig.bEffective = true;

		// 
		sQueuedAnyJobCount  = 0;
		sQueuedForeJobCount = 0;

		//
		int32 kForegroundWorkerNum;
		int32 kBackgroundWorkerNum;
		{
			if (kDesiredWorkerNum == 1)
			{
				kForegroundWorkerNum = 0;
				kBackgroundWorkerNum = 1; // 
			}
			else if (kDesiredWorkerNum == 2)
			{
				kForegroundWorkerNum = 1;
				kBackgroundWorkerNum = 1; // 
			}
			else if (kDesiredWorkerNum == 3)
			{
				kForegroundWorkerNum = 1;
				kBackgroundWorkerNum = 2;
			}
			else
			{
				kForegroundWorkerNum = 2; // Current max 2 foreground worker.
				kBackgroundWorkerNum = kDesiredWorkerNum - kForegroundWorkerNum;
			}
		}
		const int32 kTotalWorkerNum = kForegroundWorkerNum + kBackgroundWorkerNum;
		sConfig.foregroundWorkerNum = kForegroundWorkerNum;
		sConfig.totalWorkerNum = kTotalWorkerNum;

		// Create allocators.
		sJobAllocator.store(new JobAllocator(), std::memory_order_relaxed);
		sJobChildLinkListAllocator.store(new JobChildLinkListAllocator(), std::memory_order_relaxed);
		sJobDependencyAllocator.store(new JobDependencyAllocator(), std::memory_order_relaxed);

		// Prepare size of work queues.
		sConfig.workQueues.resize(kTotalWorkerNum);
		for (auto& queue : sConfig.globalWorkQueues)
		{
			queue = std::make_unique<GlobalQueueType>(kMaxJobCount);
		}
		std::atomic_thread_fence(std::memory_order_seq_cst);

		//
		sRuning.store(true, std::memory_order_seq_cst);

		//
		int32 workerIndex = 0;
		sConfig.workerFutures.reserve(kTotalWorkerNum);
		std::atomic<int32> workerInitCounter = 0;
		for (; workerIndex < kForegroundWorkerNum; workerIndex++)
		{
			sConfig.workerFutures.push_back(std::async(std::launch::async, [workerIndex, kForegroundWorkerNum, kBackgroundWorkerNum, &workerInitCounter]()
			{
				const std::wstring name = std::format(L"Foreground Worker #{}", workerIndex);
				workerLoop(name, workerIndex, kForegroundWorkerNum, kBackgroundWorkerNum, workerInitCounter);
			}));
		}

		for (; workerIndex < kTotalWorkerNum; workerIndex++)
		{
			sConfig.workerFutures.push_back(std::async(std::launch::async, [workerIndex, kForegroundWorkerNum, kBackgroundWorkerNum, &workerInitCounter]()
			{
				const std::wstring name = std::format(L"Background Worker #{}", workerIndex);
				workerLoop(name, workerIndex, kForegroundWorkerNum, kBackgroundWorkerNum, workerInitCounter);
			}));
		}

		while (!(workerInitCounter.load() == kTotalWorkerNum))
		{
			std::this_thread::yield();
		}

		LOG_TRACE("Jobsystem init succeed, total core can use {}, create {} foreground worker, {} background worker, left {} core for other usage.",
			kMaxCoreThreadNum, kForegroundWorkerNum, kBackgroundWorkerNum, leftFreeCoreNum);
	}
}