#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/delegate.h>
#include <graphics/resource.h>
#include <graphics/pipeline.h>
#include <ui/imgui/imgui.h>
#include <ui/imgui/imgui_internal.h>
#include <graphics/graphics.h>

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
		void render(const ApplicationTickData& tickData);

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

	// Viewport host data.
	class ImGuiViewportData : NonCopyable
	{
	public:
		explicit ImGuiViewportData(ImGuiViewport* viewport);

		~ImGuiViewportData();

		// Each viewport keep frame buffers.
		struct RenderBuffers
		{
			graphics::HostVisibleGPUBufferRef verticesBuffer = nullptr;
			graphics::HostVisibleGPUBufferRef indicesBuffer = nullptr;
		};

		RenderBuffers& getRenderBuffers(uint32 index)
		{
			while (m_frameRenderBuffers.size() <= index)
			{
				m_frameRenderBuffers.push_back({});
			}
			return m_frameRenderBuffers.at(index);
		}

		VkCommandBuffer getCommandBuffer(uint32 index);

		graphics::Swapchain& swapchain()
		{
			return *m_swapchain;
		}

		CallOnceEvents<ImGuiViewportData, const ApplicationTickData&, graphics::CommandList&> onTickWithCmds;

		void tickWithCmds(const ApplicationTickData& data)
		{
			m_lastBroadcastFrame = data.tickCount;
			onTickWithCmds.brocast(data, m_swapchain->getCommandList());
		}

		bool shouldPushTickCmds(uint64 data) const
		{
			if (m_lastBroadcastFrame == 0 || m_lastBroadcastFrame + 1 == data)
			{
				return true;
			}

			return false;
		}

	private:
		uint64 m_lastBroadcastFrame = 0;

		// Common buffers ring for current window.
		std::vector<VkCommandBuffer> m_commandBuffers;

		// Render buffers.
		std::vector<RenderBuffers> m_frameRenderBuffers;

		// Window swapchain.
		std::unique_ptr<graphics::Swapchain> m_swapchain = nullptr;
	};

}