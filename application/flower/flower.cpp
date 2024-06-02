#include "flower.h"
#include "widget/hub.h"
#include "widget/dockspace.h"
#include "widget/downbar.h"
#include "widget/console.h"

#include "manager/scene_ui_content.h"
#include "manager/project_content.h"
#include "widget/content.h"
#include "widget/asset.h"
#include "widget/viewport.h"
#include "widget/outliner.h"
#include "widget/detail.h"

using namespace chord;
using namespace chord::graphics;

constexpr auto kCacheAssetSafeFramePeriod = 4;

Flower& Flower::get()
{
	static Flower flower;
	return flower;
}

void Flower::init()
{
    m_hubHandle = m_widgetManager.addWidget<HubWidget>();
	m_onTickHandle = getContext().onTick.add([this](const ApplicationTickData& tickData) { this->onTick(tickData); });

	m_assetConfigWidgetManager = new AssetConfigWidgetManager();
	m_builtinTextures.init();


	m_onShouldClosedHandle = Application::get().onShouldClosed.add([this]() -> bool
	{
		return this->onWindowRequireClosed();
	});
}

void Flower::onTick(const chord::ApplicationTickData& tickData)
{
	// Generic widget tick.
	m_widgetManager.tick(tickData);

	// Asset widgets tick.
	m_assetConfigWidgetManager->tick(tickData);

	// Some call once event tick.
	onceEventAfterTick.brocast(tickData);

	updateApplicationTitle();
	shortcutHandle();
}

void Flower::release()
{
	m_widgetManager.release();

	m_builtinTextures = {};

	if (m_contentManager)
	{
		delete m_contentManager;
		m_contentManager = nullptr;
	}

	if (m_sceneContentManager)
	{
		delete m_sceneContentManager;
		m_sceneContentManager = nullptr;
	}

	check(getContext().onTick.remove(m_onTickHandle));
	check(Application::get().onShouldClosed.remove(m_onShouldClosedHandle));
}

void Flower::updateApplicationTitle()
{
	const auto& projectConfig = Project::get().getPath();
	if (Project::get().isSetup())
	{
		auto activeScene = Application::get().getEngine().getSubsystem<SceneManager>().getActiveScene();

		std::u16string showName = projectConfig.projectName.u16() + u" - " + activeScene->getName().u16();
		if (activeScene->isDirty())
		{
			showName += u"*";
		}

		// Update title name.
		Application::get().setMainWindowTitle(utf8::utf16to8(showName));
	}
}

void Flower::shortcutHandle()
{
	if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
	{
		if (ImGui::IsKeyPressed(ImGuiKey_S))
		{
			if (m_contentManager)
			{
				if (!m_contentManager->getDirtyAsset<Scene>().empty())
				{
					m_dockSpaceHandle->sceneAssetSave.open();
				}
			}

		}
	}
}

bool Flower::onWindowRequireClosed()
{
	if (!Project::get().isSetup())
	{
		return true;
	}

	bool bReturn = true;

	if (!m_contentManager->getDirtyAsset<Scene>().empty())
	{
		bReturn = false;
		if (m_dockSpaceHandle->sceneAssetSave.open())
		{
			check(!m_dockSpaceHandle->sceneAssetSave.afterEventAccept);
			m_dockSpaceHandle->sceneAssetSave.afterEventAccept = []()
			{
				glfwSetWindowShouldClose(Application::get().getWindowData().window, 1);
			};
		}
	}

	return bReturn;
}

int Flower::run(int argc, const char** argv)
{
	CVarSystem::get().getCVarCheck<bool>("r.log.file")->set(true);
	CVarSystem::get().getCVarCheck<bool>("r.ui.docking")->set(true);
	CVarSystem::get().getCVarCheck<bool>("r.ui.viewports")->set(true);
	
	CVarSystem::get().getCVarCheck<u16str>("r.log.file.name")->set(u16str("flower/flower"));
	CVarSystem::get().getCVarCheck<u16str>("r.ui.configPath")->set(u16str("config/flower"));

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

		// Viewport
		{
			WidgetInView viewportView = { .bMultiWindow = true };
			for (size_t i = 0; i < kMultiWidgetMaxNum; i++)
			{
				m_viewports[i] = m_widgetManager.addWidget<WidgetViewport>(i);
				viewportView.widgets[i] = m_viewports[i];
				m_viewports[i]->setVisible(false);
			}
			m_viewports[0]->setVisible(true);

			m_dockSpaceHandle->widgetInView.add(viewportView);
		}

		// Outliner and detail.
		{
			m_sceneContentManager = new UISceneContentManager();

			m_widgetOutlineHandle = m_widgetManager.addWidget<WidgetOutliner>();
			{
				WidgetInView outlineView = { .bMultiWindow = false, .widgets = { m_widgetOutlineHandle } };
				m_dockSpaceHandle->widgetInView.add(outlineView);
			}

			WidgetInView detailView = { .bMultiWindow = true };
			for (size_t i = 0; i < kMultiWidgetMaxNum; i++)
			{
				m_details[i] = m_widgetManager.addWidget<WidgetDetail>(i);
				detailView.widgets[i] = m_details[i];
				m_details[i]->setVisible(false);
			}
			m_details[0]->setVisible(true);

			m_dockSpaceHandle->widgetInView.add(detailView);
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

		return std::make_shared<GPUTextureAsset>(texture);
	};

	folderImage = flushUploadImage("resource/texture/folder.png");
	fileImage = flushUploadImage("resource/texture/file.png");
}
