#pragma once

#include <ui/widget.h>
#include "../manager/project_content.h"

#include <ui/imgui/imgui.h>
#include <ui/imgui/imgui_internal.h>
#include <asset/asset.h>

class AssetConfigWidgetManager;

class WidgetAssetConfig
{
public:
	WidgetAssetConfig(AssetConfigWidgetManager& manager, const std::filesystem::path& path);
	virtual ~WidgetAssetConfig() noexcept;

	// event always tick.
	void tick(const chord::ApplicationTickData& tickData);

	bool shouldClosed() const
	{
		return !m_bRun;
	}

	const auto& getName() const 
	{ 
		return m_name;
	}

protected:
	// Cache manager.
	AssetConfigWidgetManager& m_widgetManager;
	chord::AssetManager& m_assetManager;

	// Should run or close.
	bool m_bRun;

	// 
	chord::u16str m_name;

	// Inspect asset.
	std::shared_ptr<chord::IAsset> m_asset;
};

class AssetConfigWidgetManager
{
public:
	explicit AssetConfigWidgetManager();
	virtual ~AssetConfigWidgetManager();
	WidgetAssetConfig* openWidget(const std::filesystem::path& path);

	void tick(const chord::ApplicationTickData& tickData);

	ImGuiViewport* getTickDrawViewport() 
	{ 
		return m_tickDrawCtx.viewport; 
	}

	void setTickDrawViewport(ImGuiViewport* vp, ImGuiID dockID) 
	{ 
		m_tickDrawCtx.viewport = vp; 
		m_tickDrawCtx.dockId = dockID; 
	}
	
	ImGuiID getTickDrawDockID() const 
	{ 
		return m_tickDrawCtx.dockId; 
	}



private:
	std::unordered_map<std::filesystem::path, std::unique_ptr<WidgetAssetConfig>> m_widgets;

	// Cache viewport for tick switch.
	struct
	{
		ImGuiViewport* viewport;
		ImGuiID dockId;
	} m_tickDrawCtx;
};