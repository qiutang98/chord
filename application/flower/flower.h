#pragma once



#include "pch.h"

class ProjectContentManager;

class HubWidget;
class MainViewportDockspaceAndMenu;
class DownbarWidget;
class WidgetConsole;
class WidgetContent;



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
		chord::graphics::GPUTextureRef folderImage;
		chord::graphics::GPUTextureRef fileImage;

		void init();
	};
	const auto& getBuiltinTextures() const
	{
		return m_builtinTextures;
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
};
