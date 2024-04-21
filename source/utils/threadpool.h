#pragma once

#include <utils/utils.h>

#include <vector>
#include <future>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <type_traits>
#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

namespace chord
{
	// Check future is ready or not.
	template<typename R>
	bool isReady(std::future<R> const& f)
	{
		return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

	// Multi future container.
	template<typename T>
	struct FutureCollection
	{
		std::vector<std::future<T>> futures;

		FutureCollection(const uint32 num = 0)
			: futures(num)
		{

		}

		void combine(FutureCollection&& other)
		{
			if (futures.empty()) 
			{ 
				futures = std::move(other.futures); 
			}
			else
			{
				futures.reserve(futures.size() + other.futures.size());
				std::move(std::begin(other.futures), std::end(other.futures), std::back_inserter(futures));
			}
		}

		CHORD_NODISCARD inline std::vector<T> get()
		{
			std::vector<T> results(futures.size());
			for (auto i = 0; i < futures.size(); i++)
			{
				results[i] = futures[i].get();
			}
			return results;
		}

		inline void wait() const
		{
			for (auto i = 0; i < futures.size(); i++)
			{
				futures[i].wait();
			}
		}

		// Get current future collection progress.
		inline float getProgress() const
		{
			auto count = 0;
			for (auto i = 0; i < futures.size(); i++)
			{
				if (isReady(futures[i]))
				{
					count++;
				}
			}

			return float(count) / float(futures.size());
		}
	};

	static inline uint32 computeThreadCount(uint32 leftFreeThreadCount, uint32 hopeMaxThreadCount)
	{
		const static uint32 kMaxCoreThreadNum = std::thread::hardware_concurrency();
		uint32 maxThreadCount = std::min(hopeMaxThreadCount, kMaxCoreThreadNum - leftFreeThreadCount);
		return std::clamp(maxThreadCount, 1u, kMaxCoreThreadNum);
	}

	template<typename TaskType>
	class IGenericThreadPool : NonCopyable
	{
	public:
		explicit IGenericThreadPool(const std::wstring& name, uint32 leftFreeThreadCount, uint32 hopeMaxThreadCount,
			std::function<void()>&& worker)
			: m_name(name)
			, m_worker(worker)
		{
			m_threadCount = chord::computeThreadCount(leftFreeThreadCount, hopeMaxThreadCount);
			m_threads = std::make_unique<std::thread[]>(m_threadCount);
			createThread();
		}

		virtual ~IGenericThreadPool()
		{
			waitForTasks();
			destroyThreads();
		}

		void setPaused(bool v) { m_bPaused = v; }
		bool getPaused() const { return m_bPaused;}

		CHORD_NODISCARD size_t getTasksQueuedNum() const
		{
			const std::lock_guard tasksLock(m_taskQueueMutex);
			return m_tasksQueue.size();
		}

		CHORD_NODISCARD size_t getTasksRunningNum() const
		{
			const std::lock_guard tasks_lock(m_taskQueueMutex);
			return m_tasksQueueTotalNum - m_tasksQueue.size();
		}

		CHORD_NODISCARD size_t getTasksTotal() const
		{
			return m_tasksQueueTotalNum.load();
		}

		CHORD_NODISCARD size_t getThreadCount() const
		{
			return m_threadCount;
		}

		// Wait for all task finish, if when pause, wait for all processing task finish.
		void waitForTasks()
		{
			m_bWaiting = true;

			std::unique_lock<std::mutex> tasksLock(m_taskQueueMutex);
			m_cvTaskDone.wait(tasksLock, [this]
			{
				return (m_tasksQueueTotalNum == (m_bPaused ? m_tasksQueue.size() : 0));
			});
			m_bWaiting = false;
		}

		// Add task in queue.
		void addTask(TaskType&& task)
		{
			{
				const std::lock_guard tasksLock(m_taskQueueMutex);
				m_tasksQueue.push(std::move(task));
			}
			++m_tasksQueueTotalNum;
			m_cvTaskAvailable.notify_one();
		}

		void reset(uint32 leftFreeThreadCount, uint32 hopeMaxThreadCount = ~0)
		{
			// Store old paused state.
			const bool wasPaused = m_bPaused;

			// Clear and flush all task.
			m_bPaused = true;
			waitForTasks();
			destroyThreads();

			// Rebuild threads.
			m_threadCount = chord::computeThreadCount(leftFreeThreadCount, hopeMaxThreadCount);
			m_threads = std::make_unique<std::thread[]>(m_threadCount);

			// Restore paused state.
			m_bPaused = wasPaused;

			// Create new thread.
			createThread();
		}

	protected:
		// Generic loop body helper function.
		void loop(std::function<void(const TaskType&)>&& executor)
		{
			while (m_bRuning)
			{
				TaskType task;

				std::unique_lock<std::mutex> lockTasksQueue(m_taskQueueMutex);
				m_cvTaskAvailable.wait(lockTasksQueue, [&]
				{
					// When task queue no empty, keep moving.
					return !m_tasksQueue.empty() || !m_bRuning;
				});

				if (m_bRuning && !m_bPaused)
				{
					task = std::move(m_tasksQueue.front());
					m_tasksQueue.pop();

					// Unlock so other thread can find task from queue.
					lockTasksQueue.unlock();

                    // Execute task.
					executor(task);

					// When task finish, we need lock again to minus queue num.
					// Alway invoke new guy when waiting.
                    lockTasksQueue.lock();
					--m_tasksQueueTotalNum;
					if (m_bWaiting)
					{
						m_cvTaskDone.notify_one();
					}
				}
			}
		}

		// Create thread.
		void createThread()
		{
			m_bRuning = true;
			for (uint32 i = 0; i < m_threadCount; i++)
			{
				m_threads[i] = std::thread([this]() { if (m_worker) { m_worker(); } });
				namedThread(m_threads[i], std::format(L"{} #{}", m_name, i));
			}
		}

		// Destroy all thread with flush.
		void destroyThreads()
		{
			m_bRuning = false;
			m_cvTaskAvailable.notify_all();
			for (uint32 i = 0; i < m_threadCount; i++)
			{
				m_threads[i].join();
			}
		}


	protected:
		// Set this value to true to pause all task in this threadpool.
		// Set this value to false to enable all task in this threadpool.
		std::atomic<bool> m_bPaused = false;

		// Flag of runing or waiting.
		std::atomic<bool> m_bRuning = false;
		std::atomic<bool> m_bWaiting = false;

		std::condition_variable m_cvTaskAvailable;
		std::condition_variable m_cvTaskDone;

		// Task queue.
		mutable std::mutex m_taskQueueMutex;
		std::atomic<size_t> m_tasksQueueTotalNum = 0;
		std::queue<TaskType> m_tasksQueue;

		std::wstring m_name;
		uint32 m_threadCount = 0;
		std::unique_ptr<std::thread[]> m_threads = nullptr;

	private:
		std::function<void()> m_worker = nullptr;
	};

	template<typename...A>
	using LambdaThreadPool = IGenericThreadPool<std::function<void(A...)>>;

	class ThreadPool : public LambdaThreadPool<>
	{
	public:
		using TaskType = std::function<void()>;

		explicit ThreadPool(const std::wstring& name, uint32 leftFreeThreadCount, uint32 hopeMaxThreadCount = ~0)
			: LambdaThreadPool<>(name, leftFreeThreadCount, hopeMaxThreadCount, [this]() { worker(); })
		{

		}

		virtual ~ThreadPool()
		{

		}

		template <typename F, typename... A>
		void pushTask(const F& task, const A&... args)
		{
			{
				const std::lock_guard tasksLock(m_taskQueueMutex);
				if constexpr (sizeof...(args) == 0)
				{
					m_tasksQueue.push(TaskType(task));
				}
				else
				{
					m_tasksQueue.push(TaskType([task, args...]{ task(args...); }));
				}
			}
			++m_tasksQueueTotalNum;
			m_cvTaskAvailable.notify_one();
		}

		template <typename F, typename... A, typename R = std::invoke_result_t<std::decay_t<F>, std::decay_t<A>...>>
		CHORD_NODISCARD std::future<R> submit(const F& task, const A&... args)
		{
			std::shared_ptr<std::promise<R>> taskPromise = std::make_shared<std::promise<R>>();

			pushTask([task, args..., taskPromise]
			{
				if constexpr (std::is_void_v<R>)
				{
					task(args...);
					taskPromise->set_value();
				}
				else
				{
					taskPromise->set_value(task(args...));
				}
			});
			return taskPromise->get_future();
		}

		// Parallel loop with indexing.
		// Usage example:
		/*
			const auto loop = [&vector](const size_t loopStart, const size_t loopEnd)
			{
				for (size_t i = loopStart; i < loopEnd; ++i)
				{
					dosomething(vector[i]);
				}
			};
			threadpool->parallelizeLoop(0, vector.size(), loop).wait();
		**/
		template <typename F, typename T1, typename T2, typename T = std::common_type_t<T1, T2>, typename R = std::invoke_result_t<std::decay_t<F>, T, T>>
		CHORD_NODISCARD FutureCollection<R> parallelizeLoop(const T1& firstIndex, const T2& indexAfterLast, const F& loop, size_t numBlocks = 0)
		{
			T firstIndexT = static_cast<T>(firstIndex);
			T indexAfterLastT = static_cast<T>(indexAfterLast);

			if (firstIndexT == indexAfterLastT)
			{
				return FutureCollection<R>();
			}

			if (indexAfterLastT < firstIndexT)
			{
				std::swap(indexAfterLastT, firstIndexT);
			}

			if (numBlocks == 0)
			{
				numBlocks = m_threadCount;
			}

			const size_t totalSize = static_cast<size_t>(indexAfterLastT - firstIndexT);
			size_t blockSize = static_cast<size_t>(totalSize / numBlocks);
			if (blockSize == 0)
			{
				blockSize = 1;
				numBlocks = totalSize > 1 ? totalSize : 1;
			}

			FutureCollection<R> fc(numBlocks);
			for (size_t i = 0; i < numBlocks; ++i)
			{
				const T start = (static_cast<T>(i * blockSize) + firstIndexT);
				const T end = (i == numBlocks - 1) ? indexAfterLastT : (static_cast<T>((i + 1) * blockSize) + firstIndexT);
				fc.futures[i] = submit(loop, start, end);
			}
			return fc;
		}

	protected:
		void worker()
		{
			loop([](const TaskType& task) { task(); });
		}
	};
}
