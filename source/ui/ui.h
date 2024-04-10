#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/delegate.h>
#include <graphics/resource.h>
#include <graphics/pipeline.h>
#include <ui/imgui/imgui.h>
#include <ui/imgui/imgui_internal.h>

namespace chord
{
	namespace graphics
	{
		class IPipeline;
	}

	class ImGuiManager : NonCopyable
	{
		friend void imguiPushWindowStyle(ImGuiViewport*);
		friend void imguiPopWindowStyle(ImGuiViewport*);
		
		friend void imguiRenderDrawData(uint32, VkCommandBuffer, void*, std::shared_ptr<graphics::IPipeline>);
	public:
		explicit ImGuiManager();
		~ImGuiManager();

		// Call this function when frame start ui logic.
		void newFrame();

		// Main window is minimized.
		bool isMainMinimized();

		// Call ImGui::Render() inner function, call this when all imgui logic finish.
		// It produce render resource.
		void render();

		const bool isWidgetDrawing() const
		{
			return m_bWidgetDrawing;
		}

	private:
		// Update all monitor fonts, return first monitor's atlas.
		ImFontAtlas* updateMonitorFonts();

		// Setup font relative resource.
		void setupFont(uint32 fontSize, float dpiScale);

		// Update ui style.
		void updateStyle();

	private:
		// UI backbuffer clear value.
		math::vec4 m_clearColor = { 0.45f, 0.55f, 0.60f, 1.00f };

		// UI ini layout config file store path.
		std::string m_iniFileStorePath;

		struct FontAtlas
		{
			float dpiScale = 0.0f;
			graphics::GPUTextureRef texture = nullptr;

			ImFontAtlas atlas; // Need to call clear when release.
			ImGuiStyle styles;
		};
		std::map<uint32, FontAtlas> m_fontAtlasTextures;
		std::set<uint32> m_fontSet;

		// Cache main atlas when init.
		ImFontAtlas* m_mainAtlas;

		// Cache style.
		ImGuiStyle m_cacheStyle;

		// Is widget drawing or not.
		bool m_bWidgetDrawing = false;
	};
}