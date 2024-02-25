#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/hashstringkey.h>
#include <utils/log.h>

namespace chord
{
	class SubsystemTickData
	{
	public:
		uint64 tickCount = 0;
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

		inline bool init(HashStringKey hash)
		{
			m_hash = hash;
			if (onInit())
			{
				LOG_TRACE("Init subsystem: '{0}'.", m_name);
				return true;
			}

			m_hash = { };
			return false;
		}

		inline bool tick(const SubsystemTickData& tickData)
		{
			CHECK(m_hash.isValid());
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
		HashStringKey m_hash { };
	};

	template<typename T>
	constexpr void checkIsBasedOnSubsystem()
	{
		static_assert(std::is_base_of<ISubsystem, T>::value, 
			"This type doesn't based on ISubsystem.");
	}
}

