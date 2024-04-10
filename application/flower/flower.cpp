#include "flower.h"
#include "widget/hub.h"
#include "widget/dockspace.h"
#include "widget/downbar.h"
#include "widget/console.h"

#include "manager/project_content.h"
#include "widget/content.h"

using namespace chord;
using namespace chord::graphics;

Flower& Flower::get()
{
	static Flower flower;
	return flower;
}

void Flower::init()
{
    m_hubHandle = m_widgetManager.addWidget<HubWidget>();
	m_onTickHandle = getContext().onTick.add([this](const ApplicationTickData& tickData) { this->onTick(tickData); });


	m_builtinTextures.init();
}

void Flower::onTick(const chord::ApplicationTickData& tickData)
{
	m_widgetManager.tick(tickData);

	onceEventAfterTick.brocast(tickData);
}

void Flower::release()
{
	m_builtinTextures = {};

	delete m_contentManager; 
	m_contentManager = nullptr;

	check(getContext().onTick.remove(m_onTickHandle));
}


int Flower::run(int argc, const char** argv)
{
	CVarSystem::get().getCVarCheck<bool>("r.log.file")->set(true);
	CVarSystem::get().getCVarCheck<bool>("r.ui.docking")->set(true);
	// CVarSystem::get().getCVarCheck<bool>("r.ui.viewports")->set(true);
	
	CVarSystem::get().getCVarCheck<std::string>("r.log.file.name")->set("flower/flower");

	
	CVarSystem::get().getCVarCheck<std::string>("r.ui.configPath")->set("config/flower");

	Application::InitConfig config { };

	// basic config.
	config.appName = "flower";
	config.iconPath = "resource/texture/flowerIcon.png";
	config.bResizable = false;
	config.windowShowMode = EWindowMode::Free;
	config.width = 1600u;
	config.height = 900u;

	// Graphics config.
	config.graphicsConfig.bDebugUtils = true;
	config.graphicsConfig.bValidation = true;
	config.graphicsConfig.bRaytracing = true;
	config.graphicsConfig.bHDR = false;

	if (Application::get().init(config))
	{
		// Init flower.
		this->init();

		// Main loop.
		Application::get().loop();

		// Release flower first.
		this->release();
		Application::get().release();
	}

	return 0;
}

void Flower::setTitle(const std::string& name, bool bAppPrefix) const
{
	std::string finalTitle = name;
	if (bAppPrefix)
	{
		finalTitle = std::format("{0} - {1}", Application::get().getName(), name);
	}

	glfwSetWindowTitle(Application::get().getWindowData().window, finalTitle.c_str());
}

void Flower::onProjectSetup()
{
    check(m_hubHandle);

    onceEventAfterTick.add([&](const chord::ApplicationTickData&)
    {
        // Remove hub.
        check(m_widgetManager.removeWidget(m_hubHandle));
        m_hubHandle = nullptr;

        // Then create all widgets.
        m_dockSpaceHandle = m_widgetManager.addWidget<MainViewportDockspaceAndMenu>();
        m_downbarHandle = m_widgetManager.addWidget<DownbarWidget>();

		// Console.
		{
			m_consoleHandle = m_widgetManager.addWidget<WidgetConsole>();
		
			// Register console in view.
			{
				WidgetInView consoleView = { .bMultiWindow = false, .widgets = { m_consoleHandle } };
				m_dockSpaceHandle->widgetInView.add(consoleView);
			}
		}

		// Content
		{
			m_contentManager = new ProjectContentManager();

			WidgetInView contentView = { .bMultiWindow = true };
			for (auto i = 0; i < kMultiWidgetMaxNum; i++)
			{
				m_contents[i] = m_widgetManager.addWidget<WidgetContent>(i);
				contentView.widgets[i] = m_contents[i];
			}
			m_contents[0]->setVisible(true);

			m_dockSpaceHandle->widgetInView.add(contentView);
		}


		{
			const auto window = Application::get().getWindowData().window;

			// Window resizable.
			glfwSetWindowAttrib(window, GLFW_RESIZABLE, GL_TRUE);

			// Maximized window.
			glfwMaximizeWindow(window);
		}
    });
}

void Flower::BuiltinTextures::init()
{
	auto flushUploadImage = [&](const char* path)
	{
		auto ldrImage = std::make_unique<ImageLdr2D>();
		check(ldrImage->fillFromFile(path));

		auto imageCI = helper::buildBasicUploadImageCreateInfo(ldrImage->getWidth(), ldrImage->getHeight(), VK_FORMAT_R8G8B8A8_SRGB);
		auto uploadVMACI = helper::buildVMAUploadImageAllocationCI();

		auto texture = std::make_shared<GPUTexture>(path, imageCI, uploadVMACI);
		getContext().syncUploadTexture(*texture, ldrImage->getSizeBuffer());

		return texture;
	};

	folderImage = flushUploadImage("resource/texture/folder.png");
	fileImage = flushUploadImage("resource/texture/file.png");
}
