#pragma once

#include "../pch.h"

#include <ui/widget.h>
#include <ui/ui_helper.h>
#include <asset/asset_common.h>

struct WidgetInView
{
	bool bMultiWindow;
	std::array<chord::IWidget*, kMultiWidgetMaxNum> widgets;
};

class ContentAssetImportWidget : public chord::ui::ImGuiPopupSelfManagedOpenState
{
public:
	explicit ContentAssetImportWidget(const std::string& titleName);

	std::string typeName = {};
	std::function<void()> afterEventAccept = nullptr;

	std::vector<chord::IAssetImportConfigRef> importConfigs = {};


protected:
	virtual void onDraw() override;
	virtual void onClosed() override
	{
		typeName = {};
		afterEventAccept = nullptr;
		importConfigs = {};
		m_bImporting  = false;
	}

	void onDrawState();
	void onDrawImporting();

	// Import progress handle.
	struct ImportProgress
	{
		chord::EventHandle logHandle{ };
		std::deque<std::pair<chord::ELogType, std::string>> logItems{ };
	} m_importProgress{ };

	// Import execut futures.
	chord::FutureCollection<void> m_executeFutures = {};

	// The assets is importing?
	bool m_bImporting = false;

};

// Control main viewport dockspace of the windows.
class MainViewportDockspaceAndMenu : public chord::IWidget
{
public:
	explicit MainViewportDockspaceAndMenu();

	// Multi widget.
	chord::RegisterManager<WidgetInView> widgetInView;

	ContentAssetImportWidget contentAssetImport;

protected:
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	void drawDockspaceMenu();
};