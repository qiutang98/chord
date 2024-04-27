#pragma once

#include <ui/widget.h>
#include "../manager/scene_ui_content.h"
#include "outliner.h"

class WidgetDetail : public chord::IWidget
{
public:
	WidgetDetail(size_t index);

protected:
	// event init.
	virtual void onInit() override;

	// event always tick.
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	// event when widget visible tick.
	virtual void onVisibleTick(const chord::ApplicationTickData& tickData) override;

	// event release.
	virtual void onRelease() override;

	void drawComponent(chord::SceneNodeRef node);

private:
	void onOutlinerSelectionChange(Selection<SceneNodeSelctor>& s);

private:
	// Index of content widget.
	size_t m_index;

	ImGuiTextFilter m_filter;
	bool m_bLock = false;

	chord::SceneNodeWeak m_inspectNode;
	chord::EventHandle m_onSelectorChange;
};
