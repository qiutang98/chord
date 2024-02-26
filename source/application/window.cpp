#include <application/window.h>
#include <application/application.h>
#include <utils/log.h>

namespace chord
{
	Window::Window(const InitConfig config)
	{
		// Must init application before create a window.
		check(Application::get().isValid());
		using ShowModeEnum = InitConfig::EWindowMode;

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, config.bResizable ? GL_TRUE : GL_FALSE);

		// We create window in primary monitor.
		auto* monitor = glfwGetPrimaryMonitor();

		// Get monitor information.
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);

		if (config.windowShowMode == ShowModeEnum::FullScreenWithoutTile)
		{
			glfwWindowHint(GLFW_RED_BITS,     mode->redBits);
			glfwWindowHint(GLFW_GREEN_BITS,   mode->greenBits);
			glfwWindowHint(GLFW_BLUE_BITS,    mode->blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
			glfwWindowHint(GLFW_DECORATED,    GLFW_FALSE);

			m_window.window = glfwCreateWindow(mode->width, mode->height, config.name.c_str(), monitor, nullptr);
		}
		else
		{
			check(
				config.windowShowMode == ShowModeEnum::FullScreenWithTile ||
				config.windowShowMode == ShowModeEnum::Free);

			auto clampWidth  = std::max(1U, config.width);
			auto clampHeight = std::max(1U, config.height);

			if (config.windowShowMode == ShowModeEnum::FullScreenWithTile)
			{
				glfwWindowHint(GLFW_MAXIMIZED, GL_TRUE);
			}

			m_window.window = glfwCreateWindow(clampWidth, clampHeight, config.name.c_str(), nullptr, nullptr);

			// Update window size.
			glfwGetWindowSize(m_window.window, &m_window.width, &m_window.height);

			// Center free mode window to center.
			if (config.windowShowMode == ShowModeEnum::Free)
			{
				glfwSetWindowPos(m_window.window, (mode->width - m_window.width) / 2, (mode->height - m_window.height) / 2);
			}
		}

		// Update window size again.
		glfwGetWindowSize(m_window.window, &m_window.width, &m_window.height);
		LOG_INFO("Create window size: ({0},{1}).", m_window.width, m_window.height);

		// Hook window user pointer.
		glfwSetWindowUserPointer(m_window.window, (void*)(&m_window));

		// UPdate icon.
		{
			const auto& appIcon = Application::get().getIcon();

			GLFWimage glfwIcon
			{ 
				.width = appIcon.getWidth(),
				.height = appIcon.getHeight(),
				.pixels = (unsigned char*)appIcon.getPixels() 
			};

			glfwSetWindowIcon(m_window.window, 1, &glfwIcon);
		}
	}

	Window::~Window()
	{
		glfwDestroyWindow(m_window.window);
	}

	bool Window::tick()
	{
		if (!m_window.bExit)
		{
			m_window.bExit |= (bool)glfwWindowShouldClose(m_window.window);
		}

		// Additional exit handle.
		if (m_window.bExit)
		{
			// On closed event intercept action and broadcast again
			if (!onBeforeClosed.isEmpty())
			{
				onBeforeClosed.broadcastRet(*this, [&](const bool* bExit) 
				{ 
					m_window.bExit &= (*bExit); 
				});
			}

			glfwSetWindowShouldClose(m_window.window, m_window.bExit);
		}

		return !m_window.bExit;
	}
}

