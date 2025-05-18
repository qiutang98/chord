#pragma once

#include <ui/widget.h>
#include "../manager/scene_ui_content.h"

class WidgetOutliner : public chord::IWidget
{
public:
	explicit WidgetOutliner();

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
	void drawSceneNode(chord::SceneNodeRef node);

	void handleEvent();

	void popupMenu();

	void beginDragDrop(chord::SceneNodeRef node);

	void acceptDragdrop(bool bRoot);

	void handleDrawState(chord::SceneNodeRef node);

	void sortChildren(chord::SceneNodeRef node);

private:
	UISceneContentManager* m_sceneManagerUI;
	chord::SceneSubSystem* m_sceneManager;

	chord::EventHandle m_onSceneUnloadHandle;
	chord::EventHandle m_onSceneLoadHandle;

	struct DrawContext
	{
		// Padding in y.
		float scenePaddingItemY = 5.0f;

		// Draw index for each draw loop.
		size_t drawIndex = 0;

		std::unordered_set<size_t> expandNodeInTreeView = {};

		// Rename input buffer.
		char inputBuffer[32];

		// Is renameing?
		bool bRenameing = false;

		// Mouse hover/left click/right click node.
		chord::SceneNodeWeak hoverNode = { };
		std::vector<chord::SceneNodeWeak> dragingNodes = { };

		static const char* kPopupMenuName;
		static const char* kOutlinerDragDropName;
	} m_drawContext;
};