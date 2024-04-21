#pragma once



#include "pch.h"

class ProjectContentManager;

class HubWidget;
class MainViewportDockspaceAndMenu;
class DownbarWidget;
class WidgetConsole;
class AssetConfigWidgetManager;
class WidgetContent;

using SnapshotCache = chord::LRUCache<chord::graphics::GPUTextureAsset, std::filesystem::path>;

class Flower
{
public:
	static Flower& get();

	// Run application.
	int run(int argc, const char** argv);

	// Set window title name.
	void setTitle(const std::string& name, bool bAppPrefix) const;

	// Event call once after tick.
	chord::CallOnceEvents<Flower, const chord::ApplicationTickData&> onceEventAfterTick;

	// 
	void onProjectSetup();

	// 
	auto& getContentManager()
	{
		return *m_contentManager; 
	}

	struct BuiltinTextures
	{
		chord::graphics::GPUTextureAssetRef folderImage;
		chord::graphics::GPUTextureAssetRef fileImage;

		void init();
	};
	const auto& getBuiltinTextures() const
	{
		return m_builtinTextures;
	}

	MainViewportDockspaceAndMenu& getDockSpace() const
	{
		return *m_dockSpaceHandle;
	}

	const SnapshotCache& getSnapshotCache() const
	{
		return *m_snapshots;
	}

	SnapshotCache& getSnapshotCache()
	{
		return *m_snapshots;
	}

	const AssetConfigWidgetManager& getAssetConfigWidgetManager() const
	{
		return *m_assetConfigWidgetManager;
	}

	AssetConfigWidgetManager& getAssetConfigWidgetManager()
	{
		return *m_assetConfigWidgetManager;
	}

private:
	explicit Flower() = default;

	// Init flower.
	void init();

	// Tick event.
	void onTick(const chord::ApplicationTickData& tickData);
	
	// Release all.
	void release();

private:
	// Tick event handle.
	chord::EventHandle m_onTickHandle;

	// Widget manager.
	chord::WidgetManager m_widgetManager;

	// UI manager.
	ProjectContentManager* m_contentManager = nullptr;

	// Widget handles.
	HubWidget* m_hubHandle = nullptr;
	MainViewportDockspaceAndMenu* m_dockSpaceHandle = nullptr;
	DownbarWidget* m_downbarHandle = nullptr;
	WidgetConsole* m_consoleHandle = nullptr;

	// Views of project content.
	std::array<WidgetContent*, kMultiWidgetMaxNum> m_contents;

	// Builtin textures.
	BuiltinTextures m_builtinTextures;

	// Lru cache.
	SnapshotCache* m_snapshots = nullptr;
	AssetConfigWidgetManager* m_assetConfigWidgetManager = nullptr;
};
