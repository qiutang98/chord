#include <utils/utils.h>
#include <utils/log.h>
#include <utils/thread.h>

namespace chord
{
	bool chord::isInMainThread()
	{
		return MainThread::get().isInThread(std::this_thread::get_id());
	}

	MainThread& MainThread::get()
	{
		static MainThread mainThrad(L"MainThread");
		return mainThrad;
	}

	void ThreadContext::init()
	{
		m_thradId = std::this_thread::get_id();
		namedCurrentThread(m_name);
	}

	void ThreadContext::beforeRelease()
	{
		while (!m_queue.isEmpty())
		{
			flush();
		}
	}

	void ThreadContext::release()
	{
		check(m_queue.isEmpty());
	}

	bool ThreadContext::isInThread(std::thread::id id) const
	{
		return m_thradId == id;
	}

	void ThreadContext::flush()
	{
		check(std::this_thread::get_id() == m_thradId);

		m_producingId ++;
		m_consumingId ++;

		TaggedTask task;
		while (!m_queue.isEmpty())
		{
			if (m_queue.getDequeue(task))
			{
				if (task.getTag() == m_consumingId)
				{
					check(m_queue.dequeue(task));

					task.getPointer()->execute<void>();
					task.getPointer()->free();
				}
				else
				{
					// Already reach frame bound.
					check(task.getTag() == m_producingId);
					break;
				}
			}
			else
			{
				std::this_thread::yield();
			}
		}
	}

	void MainThread::tick()
	{
		// Flush all pending task in current frame.
		flush();
	}
}