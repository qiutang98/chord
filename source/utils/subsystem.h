#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/optional.h>
#include <utils/log.h>

namespace chord
{
	class SubsystemTickData
	{
	public:
		ApplicationTickData appTickDaata;
	};

	class ISubsystem : NonCopyable
	{
	public:
		explicit ISubsystem(const std::string& name)
			: m_name(name)
		{

		}

		virtual ~ISubsystem() = default;

		// Get subsystem name.
		inline const auto& getName() const 
		{ 
			return m_name; 
		}

		inline const auto& getHash() const
		{
			return m_hash;
		}

		inline bool init(const std::string& hash)
		{
			m_hash = hash;
			if (onInit())
			{
				LOG_TRACE("Init subsystem: '{0}'.", m_name);
				return true;
			}

			m_hash.clear();
			return false;
		}

		inline bool tick(const SubsystemTickData& tickData)
		{
			check(!m_hash.empty());
			return onTick(tickData);
		}

		inline void release()
		{
			onRelease();
			LOG_TRACE("Release subsystem: '{0}'.", m_name);
		}

		virtual void registerCheck() { }
		virtual void beforeRelease() { }

	protected:
		virtual bool onInit() = 0;
		virtual bool onTick(const SubsystemTickData& tickData) = 0;
		virtual void onRelease() = 0;

	protected:
		std::string m_name;
		std::string m_hash { };
	};

	template<typename T>
	constexpr void checkIsBasedOnSubsystem()
	{
		static_assert(std::is_base_of<ISubsystem, T>::value, 
			"This type doesn't based on ISubsystem.");
	}
}

