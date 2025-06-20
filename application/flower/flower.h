#pragma once



#include "pch.h"

class ProjectContentManager;

class HubWidget;
class MainViewportDockspaceAndMenu;
class DownbarWidget;
class WidgetConsole;
class AssetConfigWidgetManager;
class WidgetContent;
class WidgetViewport;
class WidgetDetail;
class UISceneContentManager;
class UIWorldContentManager;
class WidgetOutliner;
class WidgetHierarchy;
class WidgetSystem;

class Flower
{
public:
	static Flower& get();

	// Run application.
	int run(int argc, const char** argv);

	// Set window title name.
	void setTitle(const std::string& name, bool bAppPrefix) const;

	// Event call once after tick.
	chord::CallOnceEvents<const chord::ApplicationTickData&> onceEventAfterTick;

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

	const AssetConfigWidgetManager& getAssetConfigWidgetManager() const
	{
		return *m_assetConfigWidgetManager;
	}

	AssetConfigWidgetManager& getAssetConfigWidgetManager()
	{
		return *m_assetConfigWidgetManager;
	}

	const UISceneContentManager& getUISceneContentManager() const
	{
		return *m_sceneContentManager;
	}

	UISceneContentManager& getUISceneContentManager()
	{
		return *m_sceneContentManager;
	}

	const UIWorldContentManager& getUIWorldContentManager() const
	{
		return *m_worldContentManager;
	}

	UIWorldContentManager& getUIWorldContentManager()
	{
		return *m_worldContentManager;
	}

	const auto* getActiveViewportCamera() const { return m_activeViewportCamera; }
	void setActiveViewportCamera(const chord::ICamera* in) { m_activeViewportCamera = in; }

private:
	explicit Flower() = default;

	// Init flower.
	void init();

	// Tick event.
	void onTick(const chord::ApplicationTickData& tickData);
	
	// Release all.
	void release();

	void updateApplicationTitle();
	void shortcutHandle();

	bool onWindowRequireClosed();

private:
	// Tick event handle.
	chord::EventHandle m_onTickHandle;
	chord::EventHandle m_onShouldClosedHandle;

	// Widget manager.
	chord::WidgetManager m_widgetManager;

	// UI manager.
	ProjectContentManager* m_contentManager = nullptr;

	WidgetSystem* m_widgetSystemHandle = nullptr;

	// Widget handles.
	HubWidget* m_hubHandle = nullptr;
	MainViewportDockspaceAndMenu* m_dockSpaceHandle = nullptr;
	DownbarWidget* m_downbarHandle = nullptr;
	WidgetConsole* m_consoleHandle = nullptr;

	// Views of project content.
	std::array<WidgetContent*, kMultiWidgetMaxNum> m_contents;

	// Builtin textures.
	BuiltinTextures m_builtinTextures;

	AssetConfigWidgetManager* m_assetConfigWidgetManager = nullptr;

	// Views of project content.
	std::array<WidgetViewport*, kMultiWidgetMaxNum> m_viewports;
	
	UISceneContentManager* m_sceneContentManager = nullptr;
	WidgetOutliner* m_widgetOutlineHandle = nullptr;
	std::array<WidgetDetail*, kMultiWidgetMaxNum> m_details;

	UIWorldContentManager* m_worldContentManager = nullptr;
	WidgetHierarchy* m_widgetHierarchyHandle = nullptr;


	const chord::ICamera* m_activeViewportCamera = nullptr;
};
