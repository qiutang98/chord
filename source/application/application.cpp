
#include <application/application.h>
#include <utils/utils.h>
#include <filesystem>
#include <utils/log.h>
#include <format>

namespace chord
{
    Application& Application::get()
    {
        static Application app { };
        return app;
    }

    Window& Application::createWindow(const Window::InitConfig& config)
    {
        m_windows.push_back(std::make_unique<Window>(config));
        return *m_windows.back();
    }

    bool Application::init(const std::string& appName, std::string iconPath)
    {
        CHECK(!m_bInit);

        // GLFW init.
        if (glfwInit() != GLFW_TRUE)
        {
            LOG_ERROR("Fail to init glfw, application will pre-exit!");
            return false;
        }

        // Load icon.
        {
            m_icon = std::make_unique<ImageLdr2D>();

            if (!iconPath.empty())
            {
                const bool bIconValid = std::filesystem::exists(iconPath) && std::filesystem::is_regular_file(iconPath);
                if (bIconValid)
                {
                    if (!m_icon->fillFromFile(iconPath))
                    {
                        const auto path = std::filesystem::absolute(iconPath).string();
                        LOG_WARN("Fail to load app icon file in disk {}, will use default icon as fallback.", path);
                    }
                }
                else
                {
                    const auto path = std::filesystem::absolute(iconPath).string();
                    LOG_WARN("Unvalid icon file path {}, will use default icon as fallback.", path);
                }
            }

            // Fallback.
            if (m_icon->isEmpty())
            {
                m_icon->fillChessboard(255, 255, 255, 255, 0, 0, 0, 255, 36, 36, 12);
            }
        }


        // Update app name in title.
        m_name = appName;
        setApplicationTitle(std::format("{} console", m_name));

        // Broadcast init events.
        onInit.broadcast();

        // Finally update init state.
        m_bInit = true;

        return m_bInit;
    }

    void Application::loop()
    {
        while (m_bLooping)
        {
            glfwPollEvents();

            // Loop and try erase window which no-longer need loop.
            m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(), [](auto& window) 
            {
                return !window->tick();
            }), m_windows.end());

            // When windows empty, break application looping.
            if (m_windows.empty())
            {
                m_bLooping = false;
            }
        }
    }

    void Application::release()
    {
        // Broadcast release event.
        onRelease.broadcast();

        // Clear all windows.
        m_windows.clear();


        glfwTerminate();
    }
}


