#pragma once
#include <utils/utils.h>
#include <utils/mpsc_queue.h>
#include <utils/job_system.h>

namespace chord
{
	class ThreadContext
	{
	private:
		using CallBackFunc = std::function<void()>;
		MPSCQueue<CallBackFunc, MPSCQueueHeapAllocator<CallBackFunc>> m_callbacks;

		std::wstring m_name;
		std::thread::id m_thradId;

		ThreadContext(const std::wstring& name)
			: m_name(name)
		{

		}

		void flush();

	public:
		static ThreadContext& main();

		void init();
		void tick(uint64 frameIndex);
		void beforeRelease();
		void release();

		bool isInThread(std::thread::id id);

		// Push task need sync in tick.
		void pushAnyThread(std::function<void()>&& task); 
	};

	// Main thread: Engine record vulkan command, submit, present thread.
	//              Almost vulkan operation work here.
	extern bool isInMainThread();

	// Enqueue one command in main thread, execute in next tick.
	#define ENQUEUE_MAIN_COMMAND(...) ThreadContext::main().pushAnyThread(__VA_ARGS__)
}