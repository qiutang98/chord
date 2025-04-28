#include <utils/utils.h>
#include <utils/log.h>
#include <utils/thread.h>

namespace chord
{
	bool chord::isInMainThread()
	{
		return ThreadContext::main().isInThread(std::this_thread::get_id());
	}

	ThreadContext& ThreadContext::main()
	{
		static ThreadContext mainThrad(L"MainThread");
		return mainThrad;
	}

	void ThreadContext::init()
	{
		m_thradId = std::this_thread::get_id();
		namedCurrentThread(m_name);
	}

	void ThreadContext::tick(uint64 frameIndex)
	{
		flush();
	}

	void ThreadContext::beforeRelease()
	{
		flush();
	}

	void ThreadContext::release()
	{
		check(m_callbacks.isEmpty());
	}

	bool ThreadContext::isInThread(std::thread::id id)
	{
		return m_thradId == id;
	}

	void ThreadContext::pushAnyThread(std::function<void()>&& task)
	{
		m_callbacks.enqueue(std::move(task));
	}

	void ThreadContext::flush()
	{
		check(std::this_thread::get_id() == m_thradId);
		while (!m_callbacks.isEmpty())
		{
			CallBackFunc func;
			if (m_callbacks.dequeue(func))
			{
				func();
			}
			else
			{
				break;
			}
		}
	}
}

