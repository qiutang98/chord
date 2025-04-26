#include "test.h"

#include <utils/log.h>
#include <utils/cvar.h>

int main(int argc, const char** argv)
{
	try
	{
		// auto future_work_stealing_queue = std::async(std::launch::async, []() { chord::test::work_stealing_queue::test(); });
		// auto future_mpsc_queue = std::async(std::launch::async, []() { chord::test::mpsc_queue::test(); });
		// auto future_mpmc_queue = std::async(std::launch::async, []() { chord::test::mpmc_queue::test(); });
		auto future_job_system = std::async(std::launch::async, []() { chord::test::job_system::test(); });

		// future_work_stealing_queue.wait();
		// future_mpsc_queue.wait();
		// future_mpmc_queue.wait();
		future_job_system.wait();
	}
	catch (...)
	{
		LOG_ERROR("Test failed!");
	}

	std::exit(0);
}