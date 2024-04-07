#pragma once

#include "../pch.h"

#include <ui/widget.h>

struct WidgetInView
{
	bool bMultiWindow;
	std::array<chord::IWidget*, kMultiWidgetMaxNum> widgets;
};

// Control main viewport dockspace of the windows.
class MainViewportDockspaceAndMenu : public chord::IWidget
{
public:
	explicit MainViewportDockspaceAndMenu();

	// Multi widget.
	chord::RegisterManager<WidgetInView> widgetInView;

protected:
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	void drawDockspaceMenu();
};