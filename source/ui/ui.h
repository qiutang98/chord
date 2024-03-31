#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/delegate.h>
#include <graphics/resource.h>
#include <graphics/pipeline.h>
#include <ui/imgui/imgui.h>

namespace chord
{
	class ImGuiManager : NonCopyable
	{
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

		// Call render frame after render() call.
		// It record render command.
		void renderFrame(uint32 backBufferIndex);

		// Call this after renderFrame() call.
		// It update remainder windows.
		void updateRemainderWindows();

		// Get command buffer in frame index, auto increment when no enough.
		VkCommandBuffer getCommandBuffer(uint32 index);

	private:
		// Setup font relative resource.
		void setupFont(uint32 fontSize, float dpiScale);

		// Update ui style.
		void updateStyle();

		// Render imgui draw data.
		void renderDrawData(VkCommandBuffer commandBuffer, void* drawData,  graphics::IPipelineRef pipeline);

	private:
		// Delegate handle cache when swapchain rebuild.
		EventHandle m_afterSwapChainRebuildHandle;

		// Common buffers ring.
		std::vector<VkCommandBuffer> m_commandBuffers;

		// UI backbuffer clear value.
		math::vec4 m_clearColor = { 0.45f, 0.55f, 0.60f, 1.00f };

		std::string m_iniFileStorePath;

		struct FontAtlas
		{
			graphics::GPUTextureRef texture = nullptr;
			float dpiScale = 0.0f;
			ImGuiStyle style;
			ImFontAtlas atlas; // Need to call clear when release.
		};
		std::map<uint32, FontAtlas> m_fontAtlasTextures;

		// Cache main atlas when init.
		ImFontAtlas* m_mainAtlas;
		ImGuiStyle m_mainStyle;
	};
}