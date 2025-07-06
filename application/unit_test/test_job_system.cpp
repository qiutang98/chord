#include "test.h"

#include <utils/job_system.h>
#include <random>
#include <iostream>

namespace chord::test::job_system
{
	void test()
	{
		jobsystem::init();

		jobsystem::launchSilently("HelloWorld",
			EJobFlags::Foreground, []()
			{
				LOG_TRACE("Job system say hello world!");
			});
		jobsystem::waitAllJobFinish(EBusyWaitType::None);

		{


			auto taskEventA = jobsystem::launch("A", EJobFlags::Foreground, []()
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				LOG_TRACE("Task A.");
			});

			std::vector<JobDependencyRef> taskBEvents { };
			for (int32 i = 0; i < 10; i++)
			{
				taskBEvents.push_back(jobsystem::launch("B", EJobFlags::Foreground, [i]()
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(i * 10));
					LOG_TRACE("Task B#{}.", i);
				}, { taskEventA }));
			}

			auto waitEvent = jobsystem::launch("C", EJobFlags::Foreground, []()
			{
				LOG_TRACE("Task C.");
			}, taskBEvents);

			for (int32 i = 0; i < 32; i++)
			{
				waitEvent = jobsystem::launch("D", EJobFlags::Foreground, [i]()
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(i));
					LOG_TRACE("Sequential Task D#{}.", i);
				}, { waitEvent });
			}

			waitEvent->wait(EBusyWaitType::All);
		}

		LOG_TRACE("All task finished.");

		jobsystem::release(EBusyWaitType::All);
	}
}
