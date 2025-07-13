#pragma once
#include <utils/utils.h>
#include <utils/mpsc_queue.h>
#include <utils/job_system.h>
#include <utils/mini_task.h>

namespace chord
{
	class ThreadContext
	{
	protected:
		using TaggedTask = TaggedPointer<MiniTask>;
		using JobQueueType = MPSCQueue<TaggedTask, MPSCQueueFreeListAllocator<TaggedTask>>;

		JobQueueType m_queue;
		std::wstring m_name;

		std::atomic<uint16> m_producingId =  0;
		std::atomic<uint16> m_consumingId = -1;

		std::thread::id m_thradId;
		void flush();
		ThreadContext(const std::wstring& name)
			: m_name(name)
		{

		}

	public:
		static constexpr uint32 kPersistentHighLevelThreadCount = 2; // MainThread + RenderThread

		void init();
		void beforeRelease();
		void release();

		bool isInThread(std::thread::id id) const;

		// Push task need sync in tick.
		template<typename Lambda>
		void pushAnyThread(Lambda&& task)
		{
			m_queue.enqueue(TaggedTask(MiniTask::allocate(task), m_producingId));
		}
	};

	class MainThread : public ThreadContext
	{
	private:
		std::atomic<uint64> m_mainThreadFrameId = 0;

		MainThread(const std::wstring& name)
			: ThreadContext(name)
		{

		}

	public:
		static MainThread& get();

		uint64 getFrameId() const { return m_mainThreadFrameId; }
		void tick();
	};

	extern bool isInMainThread();

	// Enqueue one command in main thread, execute in next tick.
	#define ENQUEUE_MAIN_COMMAND(...) MainThread::get().pushAnyThread(__VA_ARGS__)
}