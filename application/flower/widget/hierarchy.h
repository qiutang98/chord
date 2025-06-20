#pragma once

#include <ui/widget.h>
#include "../manager/world_ui_context.h"

class WidgetHierarchy : public chord::IWidget
{
public:
	explicit WidgetHierarchy();

protected:
	// event init.
	virtual void onInit() override;

	// event always tick.
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	// event when widget visible tick.
	virtual void onVisibleTick(const chord::ApplicationTickData& tickData) override;

	// event release.
	virtual void onRelease() override;

private:
	void drawEntity(entt::entity entity);

	void handleEvent();

	void popupMenu();

	void beginDragDrop(entt::entity entity);

	void acceptDragdrop(bool bRoot);

	void handleDrawState(entt::entity entity);

private:
	UIWorldContentManager* m_worldManagerUI;
	chord::WorldSubSystem* m_worldManager;

	chord::EventHandle m_onSceneUnloadHandle;
	chord::EventHandle m_onSceneLoadHandle;

	struct DrawContext
	{
		// Padding in y.
		float scenePaddingItemY = 5.0f;

		// Draw index for each draw loop.
		size_t drawIndex = 0;

		std::unordered_set<entt::entity> expandNodeInTreeView = {};

		// Rename input buffer.
		char inputBuffer[32];

		// Is renameing?
		bool bRenameing = false;

		// Mouse hover/left click/right click entity.
		entt::entity hoverEntity = entt::null;
		std::vector<entt::entity> dragingEntities = { };

		static const char* kHierarchyPopupMenuName;
		static const char* kHierarchyDragDropName;
	} m_drawContext;
};