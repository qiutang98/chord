#include "test.h"

#include <utils/work_stealing_queue.h>
#include <random>
#include <iostream>


namespace chord::test::work_stealing_queue
{
	static void test_impl(uint32 initCount, uint32 StepCount, uint32 StepBatchCount)
	{
		WorkStealingQueue<uint64> wsq { 1 << 14 };

		alignas(kCpuCachelineSize) std::atomic<uint64> sCounter = 0U;

		uint64 totalTask = 0U;
		alignas(kCpuCachelineSize) std::atomic<uint64> sTotalTask{ 0 };
		uint64 totalV = 0U;

		alignas(kCpuCachelineSize) std::atomic<uint64> stoleCount = 0U;
		uint64 popCount = 0U;
		for (uint64 i = 0; i < initCount; i++)
		{
			uint64 job = i + 39U;
			totalV += job;
			totalTask++;

			// init job.
			wsq.push(std::move(job));
		}

		std::atomic<bool> bRuning { true };

		uint64 v_t{};
		check(v_t == 0U);

		std::vector<std::future<void>> futures { };
		for (uint64 i = 0; i < std::thread::hardware_concurrency() * 4; i++)
		{
			futures.push_back(std::async(std::launch::async, [&]() 
			{
				while (bRuning)
				{
					std::optional<uint64> v = wsq.steal();
					if (v.has_value())
					{
						sCounter.fetch_add(v.value());
						stoleCount.fetch_add(1);
						sTotalTask++;
					}
					else
					{
						std::this_thread::yield();
					}
				}

				while (sTotalTask < totalTask)
				{
					std::optional<uint64> v = wsq.steal();
					if (v.has_value())
					{
						sCounter.fetch_add(v.value());
						stoleCount.fetch_add(1);
						sTotalTask++;
					}
					else
					{
						std::this_thread::yield();
					}
				}

			}));
		}

		for (uint64 i = 0; i < StepCount; i++)
		{
			{
				// This thread also try to pop job.
				std::optional<uint64> v = wsq.pop();
				if (v.has_value())
				{
					sCounter.fetch_add(v.value()); // vv;
					popCount ++;
					sTotalTask ++;
				}
			}
			for (uint64 j = 0; j < StepBatchCount; j++)
			{
				uint64 job = i + 39U;
				totalV += job;
				totalTask ++;
				wsq.push(std::move(job));
			}
			{
				// This thread also try to pop job.
				std::optional<uint64> v = wsq.pop();
				if (v.has_value())
				{
					sCounter.fetch_add(v.value()); // vv;
					popCount ++;
					sTotalTask++;
				}
			}
		}

		std::atomic_thread_fence(std::memory_order_seq_cst);
		bRuning.store(false);

		for (auto& future : futures)
		{
			future.wait();
		}
		check(sCounter <= totalV);
		check(sCounter == totalV);
		LOG_TRACE("WorkStealingQueue: Total Tasks: {}; Main Thread Pop: {}; Any Threads Stole: {}.",
			totalTask, popCount, stoleCount.load());
	}

	void work_stealing_queue::test()
	{
        std::vector<uint32> indexs(10240);

        std::for_each(
            std::execution::par,
            std::begin(indexs),
            std::end(indexs),
            [](auto& val) 
            {  
                std::random_device dev;
                std::mt19937 rng(dev());

                std::uniform_int_distribution<std::mt19937::result_type> dist0_8192(1, 4096);
                std::uniform_int_distribution<std::mt19937::result_type> dist0_1024(1, 1024);
                std::uniform_int_distribution<std::mt19937::result_type> dist0_64(1, 4);

                test_impl(dist0_8192(rng), dist0_1024(rng), dist0_64(rng));
            });
	}
}