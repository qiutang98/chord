#include "scene_ui_content.h"

using namespace chord;
using namespace chord::graphics;

UISceneContentManager::UISceneContentManager()
{
	m_sceneManager = &Application::get().getEngine().getSubsystem<SceneSubSystem>();

	m_onSceneUnloadHandle = m_sceneManager->onSceneUnload.add([this](SceneRef scene) { onSceneUnload(scene);  });
	m_onSceneLoadHandle = m_sceneManager->onSceneLoad.add([this](SceneRef scene) { onSceneLoad(scene);  });
}

UISceneContentManager::~UISceneContentManager()
{
	check(m_sceneManager->onSceneUnload.remove(m_onSceneUnloadHandle));
	check(m_sceneManager->onSceneLoad.remove(m_onSceneLoadHandle));
}

void UISceneContentManager::onSceneUnload(chord::SceneRef scene)
{
	// Clear all scene selections.
	m_sceneSelections.clear();
}

void UISceneContentManager::onSceneLoad(chord::SceneRef scene)
{
	// Clear all scene selections.
	m_sceneSelections.clear();
	rebuildSceneNodeNameMap();
}

void UISceneContentManager::rebuildSceneNodeNameMap()
{
	auto activeScene = m_sceneManager->getActiveScene();
	m_cacheNodeNameMap.clear();

	activeScene->loopNodeTopToDown([&](std::shared_ptr<SceneNode> node)
	{
		m_cacheNodeNameMap.insert({ node->getName(), 0 });
	}, activeScene->getRootNode());
}

u16str UISceneContentManager::addUniqueIdForName(const u16str& name16)
{
	std::string name = name16.u8();

	std::string removePrefix = name.substr(0, name.find_last_of("("));
	removePrefix = removePrefix.substr(0, removePrefix.find_last_not_of(" \t\f\v\n\r") + 1);

	u16str removeName = u16str(removePrefix);
	size_t& id = m_cacheNodeNameMap[removeName];
	if (id == 0)
	{
		id++;
		return removeName;
	}
	else
	{
		std::string uniqueName = std::format("{} ({})", removePrefix, id);
		id++;
		return u16str(uniqueName);
	}
}
