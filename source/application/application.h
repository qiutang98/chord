#pragma once

#include <utils/engine.h>
#include <utils/utils.h>
#include <utils/image.h>
#include <utils/delegate.h>
#include <utils/subsystem.h>
#include <utils/optional.h>
#include <graphics/graphics.h>
#include <utils/timer.h>
#include <utils/threadpool.h>

namespace chord
{
	class Engine;
	class AssetManager;
	namespace graphics
	{
		class Context;
	}

	enum class EWindowMode
	{
		FullScreenWithoutTile = 0,
		FullScreenWithTile,
		Free,
		FullScreen,

		MAX
	};

	class GraphicsInitConfig
	{
	public:
		bool bDebugUtils : 1;
		bool bValidation : 1;
		bool bHDR : 1;
		bool bRaytracing : 1;
	};

	class EngineInitConfig
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
		// Application runtime period.
		ERuntimePeriod m_runtimePeriod = ERuntimePeriod::MAX;

		// Application init or not.
		bool m_bInit = false;

		// Application infos.
		std::string m_name = { };
		std::unique_ptr<ImageLdr2D> m_icon = nullptr;

		// Cache main window.
		WindowData m_windowData = { };

		// Graphics context.
		graphics::Context m_context;

		Timer m_timer;

		std::unique_ptr<ThreadPool> m_threadPool = nullptr;

		// App asset manager.
		std::unique_ptr<AssetManager> m_assetManager = nullptr;
		std::unique_ptr<Engine> m_engine = nullptr;

	public:
		static Application& get();

		inline bool isValid() const 
		{ 
			return m_bInit; 
		}

		const auto getRuntimePeriod() const
		{
			return m_runtimePeriod;
		}

		ThreadPool& getThreadPool() const
		{
			return *m_threadPool;
		}

		AssetManager& getAssetManager() const
		{
			return *m_assetManager;
		}

		Engine& getEngine() const
		{
			return *m_engine;
		}

		// Init application.
		struct InitConfig
		{
			std::string appName = "untitled";
			std::string iconPath { };

			// Main window init mode.
			EWindowMode windowShowMode = EWindowMode::FullScreenWithTile;

			// Window is resizable
			bool bResizable = true;

			// Init width and init height, only valid when window mode is free.
			uint32 width  = 800U;
			uint32 height = 480U;

			// Graphics context init config.
			GraphicsInitConfig graphicsConfig;
		};
		CHORD_NODISCARD bool init(const InitConfig& config);
		
		// Application ticking loop.
		void loop();

		// Application release.
		void release();

		void close()
		{
			m_windowData.bContinue = false;
		}

		inline const ImageLdr2D& getIcon() const 
		{
			return *m_icon; 
		}

		inline const std::string& getName() const 
		{ 
			return  m_name; 
		}

		inline auto& getContext()
		{
			return m_context;
		}

		inline const auto& getContext() const
		{
			return m_context;
		}

		inline const auto& getWindowData() const
		{
			return m_windowData;
		}

		void setMainWindowTitle(const std::string& name) const;

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