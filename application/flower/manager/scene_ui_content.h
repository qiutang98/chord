#pragma once

#include "../pch.h"
#include "../selection.h"

struct SceneNodeSelctor
{
	chord::SceneNodeWeak node;
	size_t nodeId = ~0;

	explicit SceneNodeSelctor(const chord::SceneNodeRef inNode)
		: node(inNode)
	{
		if (inNode)
		{
			nodeId = inNode->getId();
		}
	}

	bool operator==(const SceneNodeSelctor& rhs) const
	{
		return nodeId == rhs.nodeId;
	}

	bool operator!=(const SceneNodeSelctor& rhs) const { return !(*this == rhs); }
	bool operator<(const SceneNodeSelctor& rhs) const { return nodeId < rhs.nodeId; }

	operator bool() const { return isValid(); }
	bool isValid() const
	{
		return (node.lock() != nullptr) && (nodeId != ~0);
	}
};
using SceneNodeSelctions = Selection<SceneNodeSelctor>;


class UISceneContentManager final : chord::NonCopyable
{
public:
	explicit UISceneContentManager();
	~UISceneContentManager();

	auto& sceneNodeSelections()
	{
		return m_sceneSelections;
	}

	const auto& sceneNodeSelections() const
	{
		return m_sceneSelections;
	}

	chord::u16str addUniqueIdForName(const chord::u16str& name);

private:
	void onSceneUnload(chord::SceneRef scene);
	void onSceneLoad(chord::SceneRef scene);

	void rebuildSceneNodeNameMap();


private:
	chord::SceneSubSystem* m_sceneManager;

	Selection<SceneNodeSelctor> m_sceneSelections;

	chord::EventHandle m_onSceneUnloadHandle;
	chord::EventHandle m_onSceneLoadHandle;

	// Cache node string map avoid rename with same name.
	std::map<chord::u16str, size_t> m_cacheNodeNameMap;
};