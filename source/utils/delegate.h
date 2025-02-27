#pragma once

#include <functional>
#include <vector>
#include <shared_mutex>

#include <utils/noncopyable.h>
#include <utils/utils.h>
#include <utils/log.h>

namespace chord
{
	template<typename Friend, typename... Args>
	class CallOnceEvents
	{
		friend Friend;
	private:
		std::shared_mutex m_lock;
		std::vector<std::function<void(Args...)>> m_collections;

		void brocast(Args&&... args)
		{
			std::unique_lock<std::shared_mutex> lock(m_lock);
			for (auto& func : m_collections)
			{
				if(func) { func(std::forward<Args>(args)...); }
			}
			m_collections.clear();
		}

	public:
		void add(std::function<void(Args...)>&& func)
		{
			std::unique_lock<std::shared_mutex> lock(m_lock);
			m_collections.push_back(func);
		}
	};

	template<typename Friend, typename RetType, typename... Args>
	class Delegate : NonCopyable
	{
		friend Friend;
	public:
		using DelegateType = std::function<RetType(Args...)>;

		Delegate() = default;

		void bind(DelegateType&& lambda)
		{
			m_lambda = std::move(lambda);
		}

		bool isBound() const
		{
			return m_lambda != nullptr;
		}

		void clear() 
		{ 
			m_lambda = nullptr; 
		}

	private:
		CHORD_NODISCARD RetType execute(Args... args) const
		{
			return m_lambda(std::forward<Args>(args)...);
		}

		CHORD_NODISCARD RetType executeIfBound(Args... args) const
		{
			if (isBound())
			{
				return m_lambda(std::forward<Args>(args)...);
			}
			return { };
		}

	private:
		DelegateType m_lambda = nullptr;
	};

	// 
	template<typename Friend, typename RetType, typename... Args>
	class MultiDelegates : NonCopyable
	{
		friend Friend;
	public:
		//
		using EventType = std::function<RetType(Args...)>;

		CHORD_NODISCARD EventHandle add(EventType&& lambda)
		{
			// Input lambda can't be nullptr.
			check(lambda != nullptr);

			//
			std::unique_lock lock(m_mutex);

			// Add a new element.
			m_events.push_back(std::make_unique<EventType>(lambda));
			m_validHandleCount++;

			// Return pointer as result.
			return reinterpret_cast<EventHandle>(m_events.back().get());
		}

		// Remove event.
		CHORD_NODISCARD bool remove(EventHandle& handle)
		{
			if (handle == nullptr)
			{
				return false;
			}

			std::unique_lock lock(m_mutex);

			bool bSuccess = false;
			const EventType* ptr = reinterpret_cast<EventType*>(handle);
			for (auto index = 0; index < m_events.size(); index++)
			{
				if (ptr == m_events[index].get())
				{
					// Reset handle.
					handle = nullptr;
					m_events[index] = nullptr;

					// Update counter.
					m_validHandleCount--;
					m_invalidHandleCount++;

					//
					bSuccess = true;
					break;
				}
			}

			constexpr size_t kShrinkPercent = /* 1 / */ 4;
			if (m_invalidHandleCount > (m_events.size() / kShrinkPercent))
			{
				m_events.erase(std::remove_if(m_events.begin(), m_events.end(), [&](const auto& x)
				{
					return x == nullptr;
				}), m_events.end());

				m_invalidHandleCount = 0;
				check(m_validHandleCount == m_events.size());
			}

			return bSuccess;
		}

		// Check event handle is bound or not.
		CHORD_NODISCARD bool isBound(const EventHandle& handle) const
		{
			if (handle == nullptr)
			{
				return false;
			}

			std::shared_lock lock(m_mutex);

			const EventType* ptr = reinterpret_cast<EventType*>(handle);
			for (const auto& event : m_events)
			{
				if (ptr == event.get())
				{
					return true;
				}
			}
			return false;
		}

		CHORD_NODISCARD const bool isEmpty() const
		{
			return m_validHandleCount == 0;
		}

	protected:
		template<typename OpType = size_t>
		void broadcast(Args...args, std::function<void(const OpType&)>&& opResult = nullptr)
		{
			std::shared_lock<std::shared_mutex> lock(m_mutex);
			for (const auto& event : m_events)
			{
				if (event == nullptr)
				{
					continue;
				}

				if constexpr (std::is_void_v<RetType>)
				{
					(*event)(std::forward<Args>(args)...);
				}
				else
				{
					RetType result = (*event)(std::forward<Args>(args)...);
					if (opResult != nullptr)
					{
						opResult(result);
					}
				}
			}
		}

	protected:
		mutable std::shared_mutex m_mutex;
		std::vector<std::unique_ptr<EventType>> m_events = { };

		uint32 m_invalidHandleCount = 0;
		std::atomic<uint32> m_validHandleCount = 0;
	};

	template<typename Friend, typename...Args>
	using Events = MultiDelegates<Friend, void, Args...>;
}