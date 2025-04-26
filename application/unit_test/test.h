#pragma once 

#include <utils/utils.h>
#include <utils/log.h>

namespace chord::test
{
	namespace work_stealing_queue
	{
		void test();
	}

	namespace mpsc_queue
	{
		void test();
	}

	namespace mpmc_queue
	{
		void test();
	}

	namespace job_system
	{
		void test();
	}
}