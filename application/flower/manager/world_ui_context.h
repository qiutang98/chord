#pragma once

#include "../pch.h"
#include "../selection.h"

struct WorldEntitySelctor
{
	entt::entity entity = entt::null;

	explicit WorldEntitySelctor(entt::entity inEntity)
		: entity(inEntity)
	{
	}

	bool operator==(const WorldEntitySelctor& rhs) const
	{
		return entity == rhs.entity;
	}

	bool operator!=(const WorldEntitySelctor& rhs) const { return !(*this == rhs); }
	bool operator<(const WorldEntitySelctor& rhs) const { return entity < rhs.entity; }

	operator bool() const 
	{ 
		return isValid(); 
	}

	bool isValid() const
	{
		return entity != entt::null;
	}
};
using WorldEntitySelctions = Selection<WorldEntitySelctor>;


class UIWorldContentManager final : chord::NonCopyable
{
public:
	explicit UIWorldContentManager();
	~UIWorldContentManager();

	auto& worldEntitySelections()
	{
		return m_worldEntitySelections;
	}

	const auto& worldEntitySelections() const
	{
		return m_worldEntitySelections;
	}

	chord::u16str addUniqueIdForName(const chord::u16str& name);

	chord::ecs::World* getActiveWorld() const
	{
		return m_activeWorld;
	}

private:
	void onWorldUnload(chord::WorldRef world);
	void onWorldLoad(chord::WorldRef world);

	void rebuildWorldEntityNameMap();


private:
	chord::WorldSubSystem* m_worldManager = nullptr;
	chord::ecs::World* m_activeWorld = nullptr;

	WorldEntitySelctions m_worldEntitySelections;

	chord::EventHandle m_onWorldUnloadHandle;
	chord::EventHandle m_onWorldLoadHandle;

	// Cache entity string map avoid rename with same name.
	std::map<chord::u16str, size_t> m_cacheEntityNameMap;
};