#pragma once

#include <utils/engine.h>
#include <utils/utils.h>
#include <utils/image.h>
#include <utils/delegate.h>
#include <utils/subsystem.h>
#include <utils/optional.h>

namespace chord
{
	class Engine;

	class EngineInitConfig
	{
	public:
		bool bDebugUtils : 1;
		bool bValidation : 1;
		bool bHDR : 1;
		bool bRaytracing : 1;
	};

	class ApplicationTickData
	{
	public:

	};

	class WindowData : NonCopyable
	{
	public:
		// Cache windows pointer.
		GLFWwindow* window = nullptr;

		int32 width;
		int32 height;

		// Application should continue run or not.
		bool bContinue = true;
	};

	class Application : NonCopyable
	{
	protected:
		// Application init or not.
		bool m_bInit = false;

		// Application infos.
		std::string m_name = { };
		std::unique_ptr<ImageLdr2D> m_icon = nullptr;

		// Cache main window.
		WindowData m_windowData = { };

		// Application own engine.
		std::unique_ptr<Engine> m_engine = nullptr;

	public:
		static Application& get();

		inline bool isValid() const 
		{ 
			return m_bInit; 
		}

		// Init application.
		struct InitConfig
		{
			std::string appName = "untitled";
			std::string iconPath { };

			// Main window init mode.
			enum class EWindowMode
			{
				FullScreenWithoutTile = 0,
				FullScreenWithTile,
				Free,
				FullScreen,

				MAX
			} windowShowMode = EWindowMode::FullScreenWithTile;

			// Window is resizable
			bool bResizable = true;

			// Init width and init height, only valid when window mode is free.
			uint32 width  = 800U;
			uint32 height = 480U;

			// Engine init config.
			EngineInitConfig engineConfig;
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

		inline auto& getEngine()
		{
			return *m_engine;
		}

		inline const auto& getEngine() const
		{
			return *m_engine;
		}

		inline const auto& getWindowData() const
		{
			return m_windowData;
		}

		// Query main window framebuffer size.
		void queryFramebufferSize(int32& width, int32& height) const;

		// Query main window framebuffer size is zero or not.
		bool isFramebufferZeroSize() const;

		// Query main window size.
		void queryWindowSize(int32& width, int32& height) const;

		// Application init delegates.
		MultiDelegates<Application, bool> onInit;

		// Return should exit or not value, if one delegate require continue(return false), the exit event will stop.
		MultiDelegates<Application, bool> onShouldClosed;

		// Application release events.
		Events<Application> onRelease;

	private:
		// Application should be a singleton.
		Application() = default;

		// Create main window for current application.
		void createMainWindow(const InitConfig& config);
	};
}