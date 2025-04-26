#include "test.h"

#include <utils/mpsc_queue.h>
#include <random>
#include <iostream>

namespace chord::test::mpsc_queue
{
	static void test_impl(size_t taskCount)
	{
		alignas(kCpuCachelineSize) std::atomic<int64> sCounter = 0;
		alignas(kCpuCachelineSize) std::atomic<int64> totalTaskV = 0;
		MPSCQueue<int64, MPSCQueuePoolAllocator<int64, 64>> queue;

		std::atomic<bool> bRuning = true;
		std::future<void> future = std::async(std::launch::async, [&]()
		{
			int64 task_v = 0;
			int64 dequeueCount = 0;

			auto loopAllTask = [&]()
			{
				while (!queue.isEmpty())
				{
					if (queue.dequeue(task_v))
					{
						sCounter.fetch_add(task_v);
						dequeueCount ++;
					}
					else
					{
						std::this_thread::yield();
					}
				}
			};

			while (bRuning)
			{
				loopAllTask();
			}

			loopAllTask();
		});

		std::vector<int64> tasks(taskCount);

		std::for_each(
			std::execution::par,
			std::begin(tasks),
			std::end(tasks),
			[&](auto& val)
			{
				std::random_device dev;
				std::mt19937 rng(dev());
				std::uniform_int_distribution<std::mt19937::result_type> gen(1, 100);

				int64 v = gen(rng);
				totalTaskV.fetch_add(v);
				queue.enqueue(std::move(v));
			});

		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		bRuning = false;
		future.wait();

		check(totalTaskV.load() == sCounter.load());
		LOG_TRACE("mpsc_queue: Total Tasks: {}; Test Pass.", taskCount);
	}

	void mpsc_queue::test()
	{
		std::vector<uint32> indexs(4096);

		std::for_each(
			std::execution::par,
			std::begin(indexs),
			std::end(indexs),
			[](auto& val)
			{
				std::random_device dev;
				std::mt19937 rng(dev());
				std::uniform_int_distribution<std::mt19937::result_type> dist(1024, 8192);

				test_impl(dist(rng));
			});
	}
}

