#pragma once

#include <ui/widget.h>
#include "../manager/scene_ui_content.h"
#include "outliner.h"

class WidgetSystem : public chord::IWidget
{
public:
	explicit WidgetSystem();

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
	chord::SceneSubSystem* m_sceneManager;
};