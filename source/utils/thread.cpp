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

		m_syncManager = std::make_unique<SyncCallbackManager>();
	}

	void ThreadContext::tick(uint64 frameIndex)
	{
		// Insert fence marker.
		m_syncManager->endOfFrame(frameIndex);

		// Now flush current frame task.
		m_syncManager->flush(frameIndex);
	}

	void ThreadContext::beforeRelease()
	{
		// Before release, we flush all event.
		m_syncManager->flush(std::numeric_limits<uint64>::max());
	}

	void ThreadContext::release()
	{
		check(m_syncManager->isEmpty());
	}

	bool ThreadContext::isInThread(std::thread::id id)
	{
		return m_thradId == id;
	}

	void ThreadContext::pushAnyThread(std::function<void()>&& task)
	{
		m_syncManager->pushAnyThread(std::move(task));
	}

	void ThreadContext::pushAnyThread(std::shared_ptr<ISyncCallback> task)
	{
		m_syncManager->pushAnyThread(task);
	}

	SyncCallbackManager::SyncCallbackManager()
	{
		// Update thread Id avoid error call.
		m_syncThreadId = std::this_thread::get_id();
	}

	void SyncCallbackManager::pushAnyThread(std::shared_ptr<ISyncCallback> task)
	{
		Callback callback {  };
		callback.object = task;

		std::unique_lock lock(m_lock);
		m_callbacks.push(callback);
	}

	void SyncCallbackManager::pushAnyThread(std::function<void()>&& task)
	{
		Callback callback{  };
		callback.function = std::move(task);

		std::unique_lock lock(m_lock);
		m_callbacks.push(callback);
	}

	void SyncCallbackManager::endOfFrame(uint64 frame)
	{
		Callback callback { };
		callback.frameCount = frame;

		std::unique_lock lock(m_lock);
		m_callbacks.push(callback);
	}

	void SyncCallbackManager::flush(uint64 frame)
	{
		// Only flush in created thread.
		check(std::this_thread::get_id() == m_syncThreadId);

		while (true)
		{
			// Load task.
			Callback callback { };
			{
				std::lock_guard lock(m_lock);

				// Queue empty, break.
				if (m_callbacks.empty())
				{
					break;
				}

				// Pop one task.
				callback = m_callbacks.front();
				m_callbacks.pop();
			}

			// End of frame marker, break.
			if (callback.frameCount >= frame)
			{
				break;
			}

			if (callback.object != nullptr)
			{
				callback.object->execute();
			}
			
			if (callback.function != nullptr)
			{
				callback.function();
			}
		}
	}

	bool SyncCallbackManager::isEmpty() const
	{
		std::lock_guard lock(m_lock);
		return m_callbacks.empty();
	}
}

