#include <application/application.h>
#include <utils/log.h>
#include <filesystem>

namespace chord
{
    Application& Application::get()
    {
        static Application app{ };
        return app;
    }

    bool Application::removeSubsystem(const std::string& key)
    {
        if (!m_subsystems.empty() && m_registeredSubsystemMap.contains(key))
        {
            const auto& id = m_registeredSubsystemMap.at(key);
            if (id.isValid())
            {

                CHECK(m_subsystems.size() > id.get());

                m_subsystems.erase(m_subsystems.begin() + id.get());
                m_registeredSubsystemMap.erase(key);

                // We need to update registered subsystem index map.
                for (std::size_t i = 0; i < m_subsystems.size(); i ++)
                {
                    m_registeredSubsystemMap[m_subsystems[i]->getHash()] = i;
                }

                return true;
            }
        }

        return false;
    }

    Window& Application::createWindow(const Window::InitConfig& config)
    {
        m_windows.push_back(std::make_unique<Window>(config));
        return *m_windows.back();
    }

    bool Application::init(const InitConfig& config)
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

            if (!config.iconPath.empty())
            {
                const bool bIconValid = 
                    std::filesystem::exists(config.iconPath) && 
                    std::filesystem::is_regular_file(config.iconPath);

                if (bIconValid)
                {
                    if (!m_icon->fillFromFile(config.iconPath))
                    {
                        const auto path = std::filesystem::absolute(config.iconPath).string();
                        LOG_WARN("Fail to load app icon file in disk {0}, will use default icon as fallback.", path);
                    }
                }
                else
                {
                    const auto path = std::filesystem::absolute(config.iconPath).string();
                    LOG_WARN("Invalid icon file path {0}, will use default icon as fallback.", path);
                }
            }

            // Fallback.
            if (m_icon->isEmpty())
            {
                m_icon->fillChessboard(0, 0, 0, 255, 255, 255, 255, 255, 36, 36, 12);
            }
        }

        // Update app name in title.
        m_name = config.appName;
        setApplicationTitle(std::format("{} console", m_name));

        // Temporal update init state.
        m_bInit = true;

        // Broadcast init events.
        onInit.broadcastRet([&](const bool* bResult) { m_bInit &= (*bResult); });

        // Return result.
        return m_bInit;
    }

    void Application::loop()
    {
        SubsystemTickData subsystemTickData{ };

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

            // Subsystem tick.
            if (!m_subsystems.empty())
            {
                std::vector<ISubsystem*> pendingSubsystems { };
                for (auto& subsystem : m_subsystems)
                {
                    if (!subsystem->tick(subsystemTickData))
                    {
                        pendingSubsystems.push_back(subsystem.get());
                    }
                }

                // BeforeRelease after all subsystem ticking finish.
                for (size_t i = pendingSubsystems.size(); i > 0; i--)
                {
                    pendingSubsystems[i - 1]->beforeRelease();
                }

                // Release after all subsystem ticking finish.
                for (size_t i = pendingSubsystems.size(); i > 0; i--)
                {
                    pendingSubsystems[i - 1]->release();
                }

                // Release after all subsystem ticking finish.
                for (size_t i = pendingSubsystems.size(); i > 0; i--)
                {
                    CHECK(removeSubsystem(pendingSubsystems[i - 1]->getHash()));
                }

                // Update tick count when end of loop.
                {
                    subsystemTickData.tickCount++;
                }
            }
        }

        // Subsystem before release.
        for (size_t i = m_subsystems.size(); i > 0; i--)
        {
            m_subsystems[i - 1]->beforeRelease();
        }
    }

    void Application::release()
    {
        // Broadcast release event.
        onRelease.broadcast();

        // Subsystem release.
        for (size_t i = m_subsystems.size(); i > 0; i--)
        {
            m_subsystems[i - 1]->release();
        }

        // Clear map.
        m_registeredSubsystemMap.clear();
        m_subsystems.clear();

        // Clear all windows.
        m_windows.clear();

        // Final terminate GLFW.
        glfwTerminate();
    }
}


