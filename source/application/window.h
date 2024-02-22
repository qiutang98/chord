#pragma once

#include <GLFW/glfw3.h>
#include <utils/noncopyable.h>
#include <utils/utils.h>
#include <utils/delegate.h>

namespace chord
{
	struct WindowData
	{
		// Cache windows pointer.
		GLFWwindow* window = nullptr;

		int32 width;
		int32 height;

		bool bExit = false;
	};

	class Window : NonCopyable
	{
	public:
		struct InitConfig
		{
			std::string name;

			enum class EWindowMode
			{
				FullScreenWithoutTile = 0,
				FullScreenWithTile,
				Free,
			} windowShowMode = EWindowMode::FullScreenWithTile;

			// Window is resizable
			bool bResizable = true;

			// Init width and init height, only valid when window mode is free.
			uint32 width  = 800U;
			uint32 height = 480U;
		};

		explicit Window(const InitConfig config);
		virtual ~Window();

		bool tick();

		const auto& getData() const { return m_window; }

		// Set window close.
		void close() { m_window.bExit = true; }

		// Return should exit or not value, if one delegate require continue, the exit event will stop.
		MultiDelegates<bool, const Window&> onClosed;

	protected:
		WindowData m_window = { };
	};
}