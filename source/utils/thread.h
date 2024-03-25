#pragma once
#include <utils/utils.h>

namespace chord
{
	class ISyncCallback
	{
	public:
		virtual void execute() = 0;
	};

	class SyncCallbackManager : NonCopyable
	{
	public:
		explicit SyncCallbackManager();

		void pushAnyThread(std::shared_ptr<ISyncCallback> task);
		void pushAnyThread(std::function<void()>&& task);

		// Insert end of frame.
		void endOfFrame(uint64 frame);

		// Flush current frame.
		void flush(uint64 frame);

		bool isEmpty() const;

	private:
		struct Callback 
		{
			uint64 frameCount = 0;

			std::shared_ptr<ISyncCallback> object = nullptr;
			std::function<void()> function = nullptr;
		};

		std::thread::id m_syncThreadId;

		mutable std::mutex m_lock;
		std::queue<Callback> m_callbacks;
	};

	class ThreadContext
	{
	private:
		std::unique_ptr<SyncCallbackManager> m_syncManager;
		std::wstring m_name;
		std::thread::id m_thradId;

		ThreadContext(const std::wstring& name)
			: m_name(name)
		{

		}

	public:
		static ThreadContext& main();

		void init();
		void tick(uint64 frameIndex);
		void beforeRelease();
		void release();

		bool isInThread(std::thread::id id);

		// Push task need sync in tick.
		void pushAnyThread(std::function<void()>&& task);
		void pushAnyThread(std::shared_ptr<ISyncCallback> task);
	};

	// Main thread: Engine record vulkan command, submit, present thread.
	//              Almost vulkan operation work here.
	extern bool isInMainThread();

	// Enqueue one command in main thread, execute in next tick.
	#define ENQUEUE_MAIN_COMMAND(...) ThreadContext::main().pushAnyThread(__VA_ARGS__)
}