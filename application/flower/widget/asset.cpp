#include "asset.h"
#include "../flower.h"
#include "dockspace.h"

#include <project.h>
#include <ui/ui_helper.h>
#include <asset/asset_common.h>
#include <asset/asset.h>
#include <nfd.h>
#include "downbar.h"
#include "dockspace.h"

using namespace chord;
using namespace chord::ui;

static inline ImGuiID getAssetConfigViewportID()
{
	static const auto kAssetConfigViewportID = ImGui::GetID("WidgetAssetConfigViewport");
	return kAssetConfigViewportID;
}

static inline void prepareAssetConfigWindow()
{
	// Set window class.
	static ImGuiWindowClass windClass;
	windClass.ClassId = ImGui::GetID("WidgetAssetConfig");
	windClass.DockingAllowUnclassed = false;
	windClass.DockingAlwaysTabBar = true;
	windClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoDockingSplitMe;
	windClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
	ImGui::SetNextWindowClass(&windClass);
}

WidgetAssetConfig::WidgetAssetConfig(AssetConfigWidgetManager& manager, const std::filesystem::path& path)
	: m_widgetManager(manager)
	, m_bRun(true)
	, m_assetManager(Application::get().getAssetManager())
{
	m_asset = m_assetManager.getOrLoadAsset<IAsset>(path, true);
	m_name = buildRelativePath(Project::get().getPath().assetPath.u16(), path);
}

WidgetAssetConfig::~WidgetAssetConfig() noexcept
{

}

void WidgetAssetConfig::tick(const ApplicationTickData& tickData)
{
	prepareAssetConfigWindow();

	ImGui::SetNextWindowSize(ImVec2(1920, 1080), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(m_name.u8().c_str(), &m_bRun, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar))
	{
		ImGui::PushID(m_name.u8().c_str());
		{
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("  FILE  "))
				{

					ImGui::EndMenu();
				}
				ImGui::Separator();

				if (ImGui::BeginMenu("  EDIT  "))
				{

					ImGui::EndMenu();
				}
				ImGui::Separator();

				if (ImGui::BeginMenu("  HELP  "))
				{

					ImGui::EndMenu();
				}
				ImGui::Separator();

				ImGui::EndMenuBar();
			}

			// Draw asset.

			// m_asset->drawAssetConfig();
		}
		ImGui::PopID();
	}

	ImGui::End();
}

AssetConfigWidgetManager::AssetConfigWidgetManager()
{

}

AssetConfigWidgetManager::~AssetConfigWidgetManager()
{
	m_widgets.clear();
}

WidgetAssetConfig* AssetConfigWidgetManager::openWidget(const std::filesystem::path& path)
{
	if (!m_widgets.contains(path))
	{
		m_widgets[path] = std::make_unique<WidgetAssetConfig>(*this, path);
	}

	const std::string widgetName = m_widgets[path]->getName().u8();
	Flower::get().onceEventAfterTick.push_back([widgetName](const chord::ApplicationTickData& tickData)
	{
		ImGui::SetWindowFocus(widgetName.c_str());
	});

	return m_widgets[path].get();
}

void AssetConfigWidgetManager::tick(const ApplicationTickData& tickData)
{
	// Clear cache viewport.
	m_tickDrawCtx.viewport = nullptr;

	if (!m_widgets.empty())
	{
		for (auto& widget : m_widgets)
		{
			widget.second->tick(tickData);
		}

		std::erase_if(m_widgets, [](const auto& item) 
		{ 
			auto const& [path, widget] = item; 
			return widget->shouldClosed(); 
		});
	}
}