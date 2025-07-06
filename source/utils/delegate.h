#pragma once

#include <utils/mini_task.h>
#include <utils/tagged_ptr.h>
#include <utils/profiler.h>
#include <utils/allocator.h>
#include <utils/mpsc_queue.h>

namespace chord
{
	template<typename... Args>
	class CallOnceEvents : NonCopyable
	{
	private:
		using TaggedPtr = TaggedPointer<MiniTask>;
		using JobQueueType = MPSCQueue<TaggedPtr, MPSCQueueFreeListAllocator<TaggedPtr>>;
		
		//
		JobQueueType m_queue;
		uint16 m_brocastId = 0;

	public:
		~CallOnceEvents()
		{
			ZoneScoped;
			TaggedPtr taggedPtr;
			while (!m_queue.isEmpty())
			{
				if (m_queue.dequeue(taggedPtr))
				{
					taggedPtr.getPointer()->free();
				}
				else
				{
					std::this_thread::yield();
				}
			}
		}

		void brocastAndFree(Args&&... args)
		{
			ZoneScoped;
			TaggedPtr taggedPtr;
			while (!m_queue.isEmpty())
			{
				if (m_queue.getDequeue(taggedPtr))
				{
					if (taggedPtr.getTag() == m_brocastId)
					{
						// Reach newly add one, so start next time loop.
						m_brocastId ++;
						continue; 
					}
					else
					{
						assert(taggedPtr.getTag() + uint16(1) == m_brocastId);
						assert(m_queue.dequeue(taggedPtr));

						auto* task = taggedPtr.getPointer();
						task->execute<void, Args...>(std::forward<Args>(args)...);
						task->free();
					}
				}
				else
				{
					std::this_thread::yield();
				}
			}

			assert(m_queue.isEmpty());
		}

		template<typename Lambda>
		void push_back(Lambda func)
		{
			m_queue.enqueue(TaggedPtr(MiniTask::allocate(func), m_brocastId));
		}
	};

	template<typename RetType, typename... Args>
	class Delegate : NonCopyable
	{
	public:
		Delegate() = default;
		~Delegate()
		{
			clear();
		}

		template<typename Lambda>
		void bind(Lambda func)
		{
			assert(!isBound());
			auto* prev = m_task.exchange(MiniTask::allocate(std::move(func)), std::memory_order_seq_cst);
			assert(prev == nullptr);
		}

		bool isBound() const
		{
			return m_task.load(std::memory_order_acquire) != nullptr;
		}

		void clear() 
		{ 
			if (auto* prev = m_task.exchange(nullptr, std::memory_order_seq_cst))
			{
				prev->free();
			}
		}

		CHORD_NODISCARD RetType execute(Args... args) const
		{
			return m_task.load(std::memory_order_acquire)->execute<RetType, Args...>(std::forward<Args>(args)...);
		}

		CHORD_NODISCARD RetType executeIfBound(Args... args) const
		{
			if (isBound())
			{
				return m_task.load(std::memory_order_acquire)->execute<RetType, Args...>(std::forward<Args>(args)...);
			}
		}

	private:
		std::atomic<MiniTask*> m_task { nullptr };
	};

	using MultiDelegatesTaggedPtr = TaggedPointer<MiniTask>;

	struct EventHandle
	{
		union 
		{
			uintptr_t handle = 39;
			MultiDelegatesTaggedPtr* target;
		};

		EventHandle()
		{
			handle = 39;
		}

		EventHandle(const EventHandle& rhs)
		{
			std::memcpy(this, &rhs, sizeof(EventHandle));
		}

		~EventHandle()
		{

		}

		bool isValid() const
		{
			return handle != 39;
		}

		EventHandle& operator=(const EventHandle& other)
		{
			if (this == &other)
			{
				return *this;
			}

			std::memcpy(this, &other, sizeof(EventHandle));
			return *this;
		}

		void markInvalid()
		{
			handle = 39;
		}
	};

	// 
	template<typename RetType, typename... Args>
	class MultiDelegates : NonCopyable
	{
		using TaggedPtr = MultiDelegatesTaggedPtr;
	protected:
		mutable std::recursive_mutex m_mutex;
		std::list<TaggedPtr> m_tasksTagged;

		//
		uint16 m_brocastId = 0;

	public:
		~MultiDelegates()
		{
			ZoneScoped;
			for (auto& taggedPtr : m_tasksTagged)
			{
				taggedPtr.getPointer()->free(); // free avoid memory leak.
			}
		}

		template<typename Lambda> // RetType(Args...)
		CHORD_NODISCARD EventHandle add(Lambda func)
		{
			ZoneScoped;
			std::lock_guard<std::recursive_mutex> lock(m_mutex);

			m_tasksTagged.emplace_back(TaggedPtr(MiniTask::allocate(func), m_brocastId));

			EventHandle result { };
			result.target = &m_tasksTagged.back();

			return std::move(result);
		}

		// Remove event.
		CHORD_NODISCARD bool remove(EventHandle& handle)
		{
			ZoneScoped;
			if (!handle.isValid())
			{
				return false;
			}

			std::lock_guard<std::recursive_mutex> lock(m_mutex);

			auto taskTaggedIter = m_tasksTagged.begin();
			while (taskTaggedIter != m_tasksTagged.end())
			{
				if ((*taskTaggedIter) == (*handle.target))
				{
					taskTaggedIter->getPointer()->free();
					m_tasksTagged.erase(taskTaggedIter);
					handle.markInvalid();
					return true;
				}
				taskTaggedIter ++;
			}
			return false;
		}

		// Check event handle is bound or not.
		CHORD_NODISCARD bool isBound(const EventHandle& handle) const
		{
			ZoneScoped;
			if (!handle.isValid())
			{
				return false;
			}

			std::lock_guard<std::recursive_mutex> lock(m_mutex);

			auto taskTaggedIter = m_tasksTagged.begin();
			while (taskTaggedIter != m_tasksTagged.end())
			{
				if ((*taskTaggedIter) == (*handle.target))
				{
					return true;
				}
				taskTaggedIter++;
			}
			return false;
		}

		CHORD_NODISCARD const bool isEmpty() const
		{
			return m_tasksTagged.empty();
		}

		template<typename OpResultLambda>
		void broadcastOp(OpResultLambda opResultLambda, Args...args)
		{
			ZoneScoped;
			std::lock_guard<std::recursive_mutex> lock(m_mutex);

			auto taskTaggedIter = m_tasksTagged.begin();
			while (taskTaggedIter != m_tasksTagged.end())
			{
				// Invoke marker step.
				m_brocastId++;

				//
				TaggedPtr taggedPtr = *taskTaggedIter;
				if (taggedPtr.getTag() == m_brocastId)
				{
					break;  // Newly add callback in the list back.
				}
				else
				{
					MiniTask* task = taggedPtr.getPointer();

					if constexpr (std::is_same_v<decltype([](){}), OpResultLambda> || std::is_void_v<RetType>)
					{
						task->execute<RetType, Args...>(std::forward<Args>(args)...);
					}
					else
					{
						RetType result = task->execute<RetType, Args...>(std::forward<Args>(args)...);
						opResultLambda(result);
					}

					taggedPtr.setTag(m_brocastId);
					taskTaggedIter++;
				}
			}
		}

		void broadcast(Args...args)
		{
			broadcastOp([](){}, std::forward<Args>(args)...);
		}
	};

	template<typename...Args>
	using ChordEvent = MultiDelegates<void, Args...>;
}