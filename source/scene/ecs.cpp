#include <scene/ecs.h>

namespace chord
{
	EntityManager::EntityManager()
	{
		m_componentMask.reserve(1024);
	}

	Entity EntityManager::createEntity()
	{
		Entity result;

		if (m_freeEntities.empty())
		{
			result = m_allocatedEntity;
			m_allocatedEntity ++;
			m_componentMask.push_back({});
		}
		else
		{
			result = m_freeEntities.front();
			m_freeEntities.pop();
		}

		m_aliveEntitiesCount ++;
		return result;
	}

	void EntityManager::freeEntity(Entity entity)
	{
		m_componentMask.at(entity).reset();
		m_freeEntities.push(entity);

		m_aliveEntitiesCount --;
	}

	void EntityManager::setComponentMask(Entity entity, const ComponentMask& mask)
	{
		m_componentMask.at(entity) = mask;
	}

	ComponentMask EntityManager::getComponentMask(Entity entity) const
	{
		return m_componentMask.at(entity);
	}

	void SystemManager::entityDestroyed(Entity entity)
	{
		for (auto const& pair : m_systems)
		{
			auto const& system = pair.second;
			system->entities.erase(entity);
		}
	}

	void SystemManager::entityComponentMaskChanged(Entity entity, ComponentMask entityComponentMask)
	{
		for (auto const& pair : m_systems)
		{
			auto const& type = pair.first;
			auto const& system = pair.second;
			auto const& systemComponentMask = m_componentMaskMap[type];

			if ((entityComponentMask & systemComponentMask) == systemComponentMask)
			{
				system->entities.insert(entity);
			}
			else
			{
				system->entities.erase(entity);
			}
		}
	}

	void ComponentManager::entityDestroyed(Entity entity)
	{
		for (auto const& pair : m_componentArrays)
		{
			auto const& component = pair.second;
			component->entityDestroyed(entity);
		}
	}
}