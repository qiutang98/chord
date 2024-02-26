#pragma once

#include <application/window.h>
#include <utils/utils.h>
#include <utils/image.h>
#include <utils/delegate.h>
#include <utils/subsystem.h>
#include <utils/optional.h>

namespace chord
{
	class Application : NonCopyable
	{
	protected:
		// Application init or not.
		bool m_bInit = false;

		// Application looping state.
		bool m_bLooping = true;

		// Application infos.
		std::string m_name = { };
		std::unique_ptr<ImageLdr2D> m_icon = nullptr;

		// Cache windows.
		std::vector<std::unique_ptr<Window>> m_windows;
		
		// Subsystem register in current application.
		std::vector<std::unique_ptr<ISubsystem>>  m_subsystems;
		std::map<std::string, OptionalSizeT> m_registeredSubsystemMap;

	private:
		// Application should be a singleton.
		Application() = default;

		// Remove subsystem.
		CHORD_NODISCARD bool removeSubsystem(const std::string& key);

	public:
		static Application& get();

		inline bool isValid() const 
		{ 
			return m_bInit; 
		}

		// Create window host to current application.
		Window& createWindow(const Window::InitConfig& config);

		// Init application.
		struct InitConfig
		{
			std::string appName = "untitled";
			std::string iconPath { };
		};
		CHORD_NODISCARD bool init(const InitConfig& config);
		
		// Application ticking loop.
		void loop();

		// Application release.
		void release();

		inline const ImageLdr2D& getIcon() const 
		{
			return *m_icon; 
		}

		inline const std::string& getName() const 
		{ 
			return  m_name; 
		}

		// Application init delegates.
		MultiDelegates<Application, bool> onInit;

		// Application release events.
		Events<Application> onRelease;


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
			CHECK(!m_registeredSubsystemMap[className].isValid());

			const auto newIndex = m_subsystems.size();

			static_assert(std::is_constructible_v<T, Args...>);
			auto subsystem = std::make_unique<T>(std::forward<Args>(args)...);

			// Register CHECK.
			subsystem->registerCheck();

			// Update index.
			m_registeredSubsystemMap[className] = newIndex;

			m_subsystems.push_back(std::move(subsystem));
			if (!m_subsystems.back()->init(className))
			{
				return false;
			}

			return true;
		}


		template<typename T>
		CHORD_NODISCARD bool unregisterSubsystem()
		{
			return removeSubsystem(getSubsystem<T>()->getHash());
		}
	};
}