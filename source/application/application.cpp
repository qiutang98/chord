#include <application/application.h>
#include <utils/log.h>
#include <filesystem>
#include <utils/thread.h>
#include <graphics/graphics.h>
#include <shader_compiler/shader.h>
#include <shader_compiler/compiler.h>
#include <ui/ui.h>
#include <graphics/buffer_pool.h>
#include <graphics/texture_pool.h>
#include <asset/asset.h>
#include <graphics/uploader.h>
#include <renderer/gpu_scene.h>
#include <asset/gltf/gltf.h>
#include <asset/gltf/gltf_helper.h>
#include <utils/job_system.h>

namespace chord
{
    static uint64 sFrameCounter = 0;
    uint64 chord::getFrameCounter()
    {
        return sFrameCounter;
    }

    Application& Application::get()
    {
        static Application app{ };
        return app;
    }

    void Application::createMainWindow(const InitConfig& config)
    {
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan need disable OpenGL context api.
		glfwWindowHint(GLFW_RESIZABLE, config.bResizable ? GL_TRUE : GL_FALSE);

		// We create window in primary monitor.
		auto* monitor = glfwGetPrimaryMonitor();

		// Get monitor information.
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);

		// Create window for different window type.
		if (config.windowShowMode == EWindowMode::FullScreen)
		{
			m_windowData.window = glfwCreateWindow(mode->width, mode->height, config.appName.c_str(), monitor, nullptr);
		}
		else if (config.windowShowMode == EWindowMode::FullScreenWithoutTile)
		{
			glfwWindowHint(GLFW_RED_BITS,     mode->redBits);
			glfwWindowHint(GLFW_GREEN_BITS,   mode->greenBits);
			glfwWindowHint(GLFW_BLUE_BITS,    mode->blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
			glfwWindowHint(GLFW_DECORATED,    GLFW_FALSE);

            m_windowData.window = glfwCreateWindow(mode->width, mode->height, config.appName.c_str(), monitor, nullptr);
		}
		else
		{
			check(
				config.windowShowMode == EWindowMode::FullScreenWithTile ||
				config.windowShowMode == EWindowMode::Free);

			auto clampWidth  = std::max(1U, config.width);
			auto clampHeight = std::max(1U, config.height);

			if (config.windowShowMode == EWindowMode::FullScreenWithTile)
			{
				glfwWindowHint(GLFW_MAXIMIZED, GL_TRUE);
			}

            m_windowData.window = glfwCreateWindow(clampWidth, clampHeight, config.appName.c_str(), nullptr, nullptr);

			// Update window size.
			glfwGetWindowSize(m_windowData.window, &m_windowData.width, &m_windowData.height);

			// Center free mode window to center.
			if (config.windowShowMode == EWindowMode::Free)
			{
				glfwSetWindowPos(m_windowData.window, (mode->width - m_windowData.width) / 2, (mode->height - m_windowData.height) / 2);
			}
		}

		// Update window size again.
		glfwGetWindowSize(m_windowData.window, &m_windowData.width, &m_windowData.height);
		LOG_INFO("Create window size: ({0},{1}).", m_windowData.width, m_windowData.height);

		// Hook window user pointer.
		glfwSetWindowUserPointer(m_windowData.window, (void*)(&m_windowData));

		// UPdate icon.
		{
			GLFWimage glfwIcon
			{ 
				.width  = m_icon->getWidth(),
				.height = m_icon->getHeight(),
				.pixels = (unsigned char*)m_icon->getPixels()
			};

			glfwSetWindowIcon(m_windowData.window, 1, &glfwIcon);
		}
    }

    bool Application::init(const InitConfig& config)
    {
        check(!m_bInit);
        m_runtimePeriod = ERuntimePeriod::Initing;

        ThreadContext::main().init();

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
                m_icon->fillChessboard(RGBA::kBlack, RGBA::kWhite, 36, 36, 12);
            }
        }

        // Update app name in title.
        m_name = config.appName;

        // Console config.
        setConsoleTitle(std::format("{} console", m_name));
        setConsoleUtf8();

        const std::vector<ConsoleFontConfig> fontTypes 
        {
            { .name = L"Consolas", .width = 0, .height = 20 },
        };
        setConsoleFont(fontTypes);

        constexpr int32 kLeftFreeCore = 1;
        jobsystem::init(kLeftFreeCore);

        // Create main window.
        createMainWindow(config);

        // Temporal update init state.
        m_bInit = true;

        // Graphics context.
        {
            graphics::Context::InitConfig contextConfig{ };

            // Hardcode configs.
            contextConfig.bGLFW = true; // Force enable.
            contextConfig.pAllocationCallbacks = nullptr;

            // Load from inputs.
            contextConfig.bDebugUtils = config.graphicsConfig.bDebugUtils;
            contextConfig.bValidation = config.graphicsConfig.bValidation;
            contextConfig.bHDR        = config.graphicsConfig.bHDR;
            contextConfig.bRaytracing = config.graphicsConfig.bRaytracing;

            m_bInit &= m_context.init(contextConfig);
        }


        // Broadcast init events if engine succeed init.
        if (m_bInit)
        {
            onInit.broadcast<bool>([&](const bool& bResult) { m_bInit &= bResult; });
        }

        m_timer.init();



        m_assetManager = std::make_unique<AssetManager>();

        // Engine context.
        {
            m_engine = std::make_unique<Engine>(*this);
            EngineInitConfig engineConfig{};

            m_bInit &= m_engine->init(engineConfig);
        }

        m_gpuScene = std::make_unique<GPUScene>();

        // Return result.
        return m_bInit;
    }

    void Application::loop()
    {
        m_runtimePeriod = ERuntimePeriod::Ticking;

        m_tickData =
        {
            .tickCount = 0,
            .totalTime = m_timer.getRuntime(),
            .fps = 60.0,
            .dt = 1.0 / 60.0,
            .bFpsUpdatedPerSecond = false,
            .fpsUpdatedPerSecond = 60.0
        };

        while (m_windowData.bContinue)
        {
            glfwPollEvents();

            const bool bFpsUpdatedPerSecond = m_timer.tick();

            sFrameCounter = m_tickData.tickCount;
            m_windowData.bContinue &= m_engine->tick(m_tickData);

            // Graphics context tick.
            m_windowData.bContinue &= m_context.tick(m_tickData);

            // Accept glfw close event.
            m_windowData.bContinue &= (glfwWindowShouldClose(m_windowData.window) == GLFW_FALSE);
            if (!m_windowData.bContinue)
            {
                // Query events if they prevent exit.
                if (!onShouldClosed.isEmpty())
                {
                    // On closed event intercept action and broadcast again.
                    onShouldClosed.broadcast<bool>([&](const bool& bExit)
                    {
                        m_windowData.bContinue |= (!bExit);
                    });
                }

                // Reupdate window close state.
                glfwSetWindowShouldClose(m_windowData.window, !m_windowData.bContinue);
            }

            // Flush sync event in main.
            ThreadContext::main().tick(m_tickData.tickCount);

            // Update tick count.
            m_tickData.tickCount = m_timer.getTickCount();
            m_tickData.dt        = m_timer.getDt();
            m_tickData.bFpsUpdatedPerSecond = bFpsUpdatedPerSecond;
            m_tickData.fpsUpdatedPerSecond  = m_timer.getFpsUpdatedPerSecond();
            m_tickData.fps       = m_timer.getFps();
            m_tickData.totalTime = m_timer.getRuntime();
        }
    }

    void Application::release()
    {


        m_runtimePeriod = ERuntimePeriod::BeforeReleasing;

        m_engine->beforeRelease();
        m_context.beforeRelease();

        // Flush sync event in main thread.
        ThreadContext::main().beforeRelease();

        m_runtimePeriod = ERuntimePeriod::Releasing;

        // Broadcast release event.
        onRelease.broadcast();

        m_engine.reset();

        // GPU post engine clear.
        m_gpuScene.reset();

        m_assetManager.release();
        m_context.release();

        // Flush sync event in main.
        ThreadContext::main().release();

        // Destroy window.
        glfwDestroyWindow(m_windowData.window);

        // Final terminate GLFW.
        glfwTerminate();

        // Wait all jobsystem task finish before release.
        jobsystem::release(EBusyWaitType::All);
    }

    void Application::setMainWindowTitle(const std::string& name) const
    {
        glfwSetWindowTitle(m_windowData.window, name.c_str());
    }

    void Application::queryFramebufferSize(int32& width, int32& height) const
    {
        glfwGetFramebufferSize(m_windowData.window, &width, &height);
    }

    void Application::queryWindowSize(int32& width, int32& height) const
    {
        glfwGetWindowSize(m_windowData.window, &width, &height);
    }

    bool Application::isFramebufferZeroSize() const
    {
        int32 width, height;
        queryFramebufferSize(width, height);
        return (width == 0) || (height == 0);
    }
}


