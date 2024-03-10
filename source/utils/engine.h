#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/log.h>
#include <utils/subsystem.h>

namespace chord
{
	class Application;
	class EngineInitConfig;
	class ApplicationTickData;

    // Common engine interface.
	class Engine : NonCopyable
	{
	protected:
		const Application& m_application;

		// Subsystem register in current application.
		SubsystemTickData m_subsystemTickData { };

		std::vector<std::unique_ptr<ISubsystem>>  m_subsystems;
		std::map<std::string, OptionalSizeT> m_registeredSubsystemMap;

	public:
		template<typename T>
		CHORD_NODISCARD inline bool existSubsystem() const
		{
			checkIsBasedOnSubsystem<T>();
			return m_registeredSubsystemMap.contains(getTypeName<T>());
		}

		CHORD_NODISCARD inline bool isSubsystemEmpty() const
		{
			return m_subsystems.empty();
		}

		template<typename T>
		CHORD_NODISCARD T& getSubsystem() const
		{
			checkIsBasedOnSubsystem<T>();
			const auto& index = m_registeredSubsystemMap.at(getTypeName<T>()).get();
			return *(static_cast<T*>(m_subsystems.at(index).get()));
		}

		template<typename T, typename...Args>
		CHORD_NODISCARD bool registerSubsystem(Args&&... args)
		{
			checkIsBasedOnSubsystem<T>();
			const char* className = getTypeName<T>();

			// Check we never register subsystem yet.
			check(!m_registeredSubsystemMap[className].isValid());

			const auto newIndex = m_subsystems.size();

			static_assert(std::is_constructible_v<T, Args...>);
			auto subsystem = std::make_unique<T>(std::forward<Args>(args)...);

			// Register check.
			subsystem->registerCheck();

			// Update index.
			m_registeredSubsystemMap[className] = newIndex;

			// Add to container and init.
			m_subsystems.push_back(std::move(subsystem));

			// NOTE: must call push back before init, may some global function require get module in init body.
			if (!m_subsystems.back()->init(className))
			{
				return false;
			}

			// Register subsystem succeed.
			return true;
		}

	public:
		explicit Engine(const Application& application);
		~Engine();

		bool init(const EngineInitConfig& config);
		bool tick(const ApplicationTickData& tickData);
		void beforeRelease();
	};
}
