#pragma once

#include <functional>
#include <vector>
#include <shared_mutex>

#include <utils/noncopyable.h>
#include <utils/utils.h>
#include <utils/log.h>

namespace chord
{
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

	class EventHandle
	{
	private:
		template<typename, typename, typename...> friend class MultiDelegates;

		static const uint32 kUnvalidId = ~0;
		uint32 m_id = kUnvalidId;

	public:
		EventHandle() = default;
		bool isValid() const { return m_id != kUnvalidId; }

		bool operator==(const EventHandle&) const = default;

	private:
		static EventHandle requireUnique();
		void reset() { m_id = kUnvalidId; }
	};

	template<typename Friend, typename RetType, typename... Args>
	class MultiDelegates : NonCopyable
	{
		friend Friend;
	public:
		using EventType = std::function<RetType(Args...)>;

		struct Event
		{
			EventType   lambda = nullptr;
			EventHandle handle = { };
		};

		CHORD_NODISCARD EventHandle add(EventType&& lambda)
		{
			// Don't add when broadcasting.
			check(!isBroadcasting());

			std::unique_lock<std::shared_mutex> lock(m_lock);

			// Loop to found first free element and register.
			for (auto index = 0; index < m_events.size(); index++)
			{
				if (!m_events[index].handle.isValid())
				{
					// Create a new event.
					m_events[index] = createEvent(std::forward<EventType>(lambda));
					return m_events[index].handle;
				}
			}

			// Add a new element.
			m_events.push_back(createEvent(std::forward<EventType>(lambda)));
			return m_events.back().handle;
		}

		// Remove event.
		CHORD_NODISCARD bool remove(EventHandle& handle)
		{
			// Don't remove when broadcasting.
			check(!isBroadcasting());

			std::unique_lock<std::shared_mutex> lock(m_lock);

			if (handle.isValid())
			{
				for (auto index = 0; index < m_events.size(); index++)
				{
					if (m_events[index].handle == handle)
					{
						std::swap(m_events[index], m_events[m_events.size() - 1]);
						m_events.pop_back();

						handle.reset();
						return true;
					}
				}
			}

			return false;
		}

		// Check event handle is bound or not.
		CHORD_NODISCARD bool isBound(const EventHandle& handle) const
		{
			std::shared_lock<std::shared_mutex> lock(m_lock);

			if (handle.isValid())
			{
				for (auto& event : m_events)
				{
					if (event.handle == handle)
					{
						return true;
					}
				}
			}
			return false;
		}

		CHORD_NODISCARD const bool isEmpty() const
		{
			std::shared_lock<std::shared_mutex> lock(m_lock);
			return m_events.empty();
		}

	protected:
		template<typename OpType = size_t>
		void broadcast(Args...args, std::function<void(const OpType&)>&& opResult = nullptr)
		{
			std::shared_lock<std::shared_mutex> lock(m_lock);

			m_broadcasting++;

			for (auto& event : m_events)
			{
				if constexpr (std::is_void_v<RetType>)
				{
					event.lambda(std::forward<Args>(args)...);
				}
				else
				{
					RetType result = event.lambda(std::forward<Args>(args)...);
					if (opResult != nullptr)
					{
						opResult(result);
					}
				}
			}

			m_broadcasting--;
		}

	protected:
		bool isBroadcasting() const
		{
			return m_broadcasting > 0;
		}

		Event createEvent(EventType&& lambda) const
		{
			Event event { };
			event.handle = EventHandle::requireUnique();
			event.lambda = lambda;
			return event;
		}

	protected:
		mutable std::shared_mutex m_lock;
		std::atomic<uint32> m_broadcasting = 0;
		std::vector<Event>  m_events = { };
	};

	template<typename Friend, typename...Args>
	using Events = MultiDelegates<Friend, void, Args...>;
}