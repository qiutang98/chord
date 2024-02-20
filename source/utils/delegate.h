#pragma once

#include <functional>
#include <vector>
#include <shared_mutex>

#include "noncopyable.h"
#include "utils.h"
#include "log.h"

namespace chord
{
	template<typename RetType, typename... Args>
	class Delegate : NonCopyable
	{
	public:
		using DelegateType = std::function<RetType(Args...)>;

		Delegate() = default;

		void bind(DelegateType&& lambda)
		{
			m_lambda = std::move(lambda);
		}

		void clear() { m_lambda = nullptr; }

		bool isBound() const
		{ 
			return m_lambda != nullptr;
		}

		[[nodiscard]] RetType execute(Args... args) const
		{
			return m_lambda(std::forward<Args>(args)...);
		}

		[[nodiscard]] RetType executeIfBound(Args... args) const
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
		template<typename...> friend class Events;

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

	template<typename... Args>
	class Events : NonCopyable
	{
	public:
		using EventType = std::function<void(Args...)>;
		struct Event
		{
			EventType   lambda = nullptr;
			EventHandle handle = { };
		};

		void broadcast(Args...args)
		{
			std::shared_lock<std::shared_mutex> lock(m_lock);

			m_broadcasting ++;
			for (const auto& event : m_events)
			{
				if (event.handle.isValid() && (event.lambda != nullptr))
				{
					event.lambda(std::forward<Args>(args)...);
				}
			}
			m_broadcasting --;
		}

		[[nodiscard]] EventHandle add(EventType&& lambda)
		{
			// Don't add when broadcasting.
			CHECK(!isBroadcasting());

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
		[[nodiscard]] bool remove(EventHandle& handle)
		{
			// Don't remove when broadcasting.
			CHECK(!isBroadcasting());

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
		[[nodiscard]] bool isBound(const EventHandle& handle) const
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

	private:
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

	private:
		std::shared_mutex m_lock;

		std::atomic<uint32> m_broadcasting = 0;
		std::vector<Event>  m_events = { };
	};
}