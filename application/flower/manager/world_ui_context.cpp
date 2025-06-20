#include "world_ui_context.h"

using namespace chord;
using namespace chord::graphics;

UIWorldContentManager::UIWorldContentManager()
{
	m_worldManager = &Application::get().getEngine().getSubsystem<WorldSubSystem>();

	m_onWorldLoadHandle = m_worldManager->onWorldLoad.add([this](WorldRef world)
	{ 
		onWorldLoad(world);
	});

	m_onWorldUnloadHandle = m_worldManager->onWorldUnload.add([this](WorldRef world)
	{ 
		onWorldUnload(world);
	});
}

UIWorldContentManager::~UIWorldContentManager()
{
	check(m_worldManager->onWorldLoad.remove(m_onWorldLoadHandle));
	check(m_worldManager->onWorldUnload.remove(m_onWorldUnloadHandle));
}

void UIWorldContentManager::onWorldUnload(WorldRef world)
{
	if (m_activeWorld)
	{
		check(world.get() == m_activeWorld);
	}

	// Clear all scene selections.
	m_worldEntitySelections.clear();

	//
	m_activeWorld = nullptr;
}

void UIWorldContentManager::onWorldLoad(WorldRef world)
{
	m_activeWorld = world.get();

	// Clear all scene selections.
	m_worldEntitySelections.clear();
	rebuildWorldEntityNameMap();
}

void UIWorldContentManager::rebuildWorldEntityNameMap()
{
	m_cacheEntityNameMap.clear();
	auto& registry = m_activeWorld->getRegistry();
	auto view = registry.view<ecs::NameComponent>();
	for (entt::entity entity : view)
	{
		const auto& nameComp = registry.get<ecs::NameComponent>(entity);
		const auto str = nameComp.name.str();


		

		size_t leftPos  = str.rfind('(');
		size_t rightPos = str.find(')', leftPos + 1);

		if (leftPos == std::string::npos || rightPos == std::string::npos)
		{
			m_cacheEntityNameMap[nameComp.name] = 0;
		}
		else
		{
			std::string removePrefix = str.substr(0, str.find_last_of("("));
			removePrefix = removePrefix.substr(0, removePrefix.find_last_not_of(" \t\f\v\n\r") + 1);
			u16str removeName = u16str(removePrefix);

			std::string numStr = str.substr(leftPos + 1, rightPos - leftPos - 1);
			size_t t = std::stoll(numStr);
			if (m_cacheEntityNameMap.contains(removeName))
			{
				m_cacheEntityNameMap[removeName] = math::max(m_cacheEntityNameMap[removeName], t);
			}
			else
			{
				m_cacheEntityNameMap[removeName] = t;
			}
		}
	}
}

u16str UIWorldContentManager::addUniqueIdForName(const u16str& name16)
{
	std::string name = name16.u8();

	std::string removePrefix = name.substr(0, name.find_last_of("("));
	removePrefix = removePrefix.substr(0, removePrefix.find_last_not_of(" \t\f\v\n\r") + 1);
	u16str removeName = u16str(removePrefix);

	size_t& id = m_cacheEntityNameMap[removeName];
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
