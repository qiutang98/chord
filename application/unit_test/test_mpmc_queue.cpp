#include "test.h"

#include <utils/mpmc_queue.h>
#include <random>
#include <iostream>

namespace chord::test::mpmc_queue
{
	static void test_impl(size_t taskCount, int32 consumerCount, int32 producerCount)
	{
		alignas(kCpuCachelineSize) std::atomic<int64> sCounter = 0;
		alignas(kCpuCachelineSize) std::atomic<int64> totalTaskV = 0;
		alignas(kCpuCachelineSize) std::atomic<int64> dequeueCount = 0;

		MPMCQueue<int64> queue(taskCount);

		std::vector<std::future<void>> futures;

		for (int32 i = 0; i < consumerCount; i++)
		{
			futures.push_back(std::async(std::launch::async, [&]()
			{
				int64 task_v = 0;

				while (dequeueCount.load() < taskCount)
				{
					if (queue.dequeue(task_v))
					{
						sCounter.fetch_add(task_v);

						dequeueCount++;
					}
					else
					{
						std::this_thread::yield();
					}
				}
			}));
		}


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

		for (auto& f : futures) { f.wait(); }

		check(totalTaskV.load() == sCounter.load());
		LOG_TRACE("mpmc_queue: Total Tasks: {}; Test Pass.", taskCount);
	}

	void mpmc_queue::test()
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

				std::uniform_int_distribution<std::mt19937::result_type> c(4, 512);
				std::uniform_int_distribution<std::mt19937::result_type> p(8, 512);
				test_impl(getNextPOT(dist(rng)), c(rng), p(rng));
			});

        
	}
}