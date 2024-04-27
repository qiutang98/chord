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

class SceneAssetSaveWidget : public chord::ui::ImGuiPopupSelfManagedOpenState
{
public:
	explicit SceneAssetSaveWidget(const std::string& titleName);

	std::function<void()> afterEventAccept = nullptr;

protected:
	virtual void onDraw() override;
	virtual void onClosed() override
	{
		afterEventAccept = nullptr;
		m_bSelected = true;
		m_processingAsset = {};
	}

private:
	bool m_bSelected = true;
	chord::AssetSaveInfo m_processingAsset = {};
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

	SceneAssetSaveWidget sceneAssetSave;
	ContentAssetImportWidget contentAssetImport;

protected:
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	void drawDockspaceMenu();
};