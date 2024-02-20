
#include <application/application.h>
#include <utils/utils.h>
#include <filesystem>
#include <utils/log.h>

namespace chord
{
    Application& Application::get()
    {
        static Application app { };
        return app;
    }

    bool Application::init(std::string_view appName, std::string_view iconPath)
    {
        CHECK(!m_bInit);

        if (!std::filesystem::exists(iconPath))
        {
            LOG_ERROR("")
        }

        m_name = appName;



        // Finally update init state.
        m_bInit = true;

        return m_bInit;
    }

    bool Application::release()
    {
        return true;
    }
}


