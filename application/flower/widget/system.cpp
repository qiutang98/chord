#include "system.h"
#include "../flower.h"

#include <ui/ui_helper.h>

using namespace chord::graphics;
using namespace chord;
using namespace chord::ui;

constexpr const char* kIconSystemTitle = ICON_FA_GEAR;

WidgetSystem::WidgetSystem()
	: IWidget(combineIcon("System", kIconSystemTitle).c_str(), combineIcon("System", kIconSystemTitle).c_str())
{

}

void WidgetSystem::onInit()
{
	m_sceneManager = &Application::get().getEngine().getSubsystem<SceneManager>();
}

void WidgetSystem::onTick(const chord::ApplicationTickData& tickData)
{

}

void WidgetSystem::onVisibleTick(const chord::ApplicationTickData& tickData)
{
	auto activeScene = m_sceneManager->getActiveScene();
	activeScene->getAtmosphereManager().drawUI(activeScene);
	activeScene->getShadowManager().drawUI(activeScene);
	activeScene->getDDGIManager().drawUI(activeScene);
}

void WidgetSystem::onRelease()
{

}