#pragma once
#include <utils/utils.h>
#include <tsl/robin_map.h>

namespace chord
{
	/**
	struct Component
	{
		static constexpr uint32 kPrepareElementCount = 1024;
	};
	**/

	// Customized version from https://austinmorlan.com/posts/entity_component_system/.
	using Entity = uint32;

	// 
	using ComponentType = uint8;
	constexpr uint32 kMaxComponentType = 32;
	using ComponentMask = std::bitset<kMaxComponentType>;

	class EntityManager
	{
	public:
		EntityManager();
		Entity createEntity();
		void freeEntity(Entity entity);

		void setComponentMask(Entity entity, const ComponentMask& mask);
		ComponentMask getComponentMask(Entity entity) const;
	private:
		std::queue<Entity> m_freeEntities;
		std::vector<ComponentMask> m_componentMask;

		Entity m_allocatedEntity = 0;
		uint32 m_aliveEntitiesCount = 0;
	};

	class IComponentArray
	{
	public:
		virtual ~IComponentArray() = default;
		virtual void entityDestroyed(Entity entity) = 0;
	};



	template<typename T>
	class ComponentArray : public IComponentArray
	{
	public:
		ComponentArray()
		{
			m_componentArray.reserve(T::kPrepareElementCount);
		}

		void insertData(Entity entity, T component)
		{
			assert(m_entityToIndexMap.find(entity) == m_entityToIndexMap.end() && 
				"Component added to same entity more than once.");

			// Put new entry at end and update the maps
			size_t newIndex = m_size;
			m_entityToIndexMap[entity]   = newIndex;
			m_indexToEntityMap[newIndex] = entity;

			// 
			if (newIndex >= m_allocatedSize)
			{
				m_componentArray.push_back(component);
				m_allocatedSize ++;
			}
			else
			{
				m_componentArray[newIndex] = component;
			}
			
			++m_size;
		}

		void removeData(Entity entity)
		{
			assert(m_entityToIndexMap.find(entity) != m_entityToIndexMap.end() && 
				"Removing non-existent component.");

			// Copy element at end into deleted element's place to maintain density
			size_t indexOfRemovedEntity = m_entityToIndexMap[entity];
			size_t indexOfLastElement   = m_size - 1;
			m_componentArray[indexOfRemovedEntity] = m_componentArray[indexOfLastElement];

			// Update map to point to moved spot
			Entity entityOfLastElement = m_indexToEntityMap[indexOfLastElement];
			m_entityToIndexMap[entityOfLastElement]  = indexOfRemovedEntity;
			m_indexToEntityMap[indexOfRemovedEntity] = entityOfLastElement;

			m_entityToIndexMap.erase(entity);
			m_indexToEntityMap.erase(indexOfLastElement);

			--m_size;
		}

		T& getData(Entity entity)
		{
			assert(m_entityToIndexMap.find(entity) != m_entityToIndexMap.end() && 
				"Retrieving non-existent component.");

			// Return a reference to the entity's component
			return m_componentArray[m_entityToIndexMap[entity]];
		}

		void entityDestroyed(Entity entity) override
		{
			if (m_entityToIndexMap.find(entity) != m_entityToIndexMap.end())
			{
				// Remove the entity's component if it existed
				removeData(entity);
			}
		}

	private:
		std::vector<T> m_componentArray;

		std::unordered_map<Entity, size_t> m_entityToIndexMap;
		std::unordered_map<size_t, Entity> m_indexToEntityMap;

		// Total size of valid entries in the array.
		size_t m_size;
		size_t m_allocatedSize = 0;
	};

	class ComponentManager
	{
	private:
		template<typename T>
		std::shared_ptr<ComponentArray<T>> getComponentArray()
		{
			const char* typeName = typeid(T).name();
			return std::static_pointer_cast<ComponentArray<T>>(m_componentArrays.at(typeName));
		}

	public:
		template<typename T>
		void registerComponent()
		{
			const char* typeName = typeid(T).name();
			assert(m_componentTypes.find(typeName) == m_componentTypes.end() && 
				"Registering component type more than once.");

			 m_componentTypes.insert({ typeName, m_nextComponentType });
			m_componentArrays.insert({ typeName, std::make_shared<ComponentArray<T>>() });

			// 
			++m_nextComponentType;
		}

		template<typename T>
		ComponentType getComponentType()
		{
			const char* typeName = typeid(T).name();
			return m_componentTypes.at(typeName);
		}

		template<typename T>
		void addComponent(Entity entity, T component)
		{
			getComponentArray<T>()->insertData(entity, component);
		}

		template<typename T>
		void removeComponent(Entity entity)
		{
			getComponentArray<T>()->removeData(entity);
		}

		template<typename T>
		T& getComponent(Entity entity)
		{
			return getComponentArray<T>()->getData(entity);
		}

		void entityDestroyed(Entity entity);

	private:
		// Map from type string pointer to a component type
		tsl::robin_map<const char*, ComponentType> m_componentTypes { };

		// Map from type string pointer to a component array
		tsl::robin_map<const char*, std::shared_ptr<IComponentArray>> m_componentArrays { };

		// The component type to be assigned to the next registered component - starting at 0
		ComponentType m_nextComponentType = 0;
	};

	struct System
	{
		std::set<Entity> entities;
	};

	class SystemManager
	{
	public:
		template<typename T>
		std::shared_ptr<T> registerSystem()
		{
			const char* typeName = typeid(T).name();
			assert(m_systems.find(typeName) == m_systems.end() && 
				"Registering system more than once.");

			auto system = std::make_shared<T>();
			m_systems.insert({ typeName, system });
			return system;
		}

		template<typename T>
		void setComponentMask(ComponentMask mask)
		{
			const char* typeName = typeid(T).name();
			assert(m_systems.find(typeName) != m_systems.end() &&
				"System used before registered.");

			m_componentMaskMap.insert({ typeName, mask });
		}

		void entityDestroyed(Entity entity);
		void entityComponentMaskChanged(Entity entity, ComponentMask entityComponentMask);

	private:
		tsl::robin_map<const char*, ComponentMask> m_componentMaskMap { }; // system component mask.
		tsl::robin_map<const char*, std::shared_ptr<System>> m_systems { };
	};
}
