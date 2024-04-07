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

		// Get command buffer in frame index, auto increment when no enough.
		VkCommandBuffer getCommandBuffer(uint32 index);

		float dpiScale() const
		{
			return m_dpiScale;
		}

	private:
		// Setup font relative resource.
		void setupFont(uint32 fontSize, float dpiScale);

		// Update ui style.
		void updateStyle();

		// Render imgui draw data.
		void renderDrawData(uint32 backBufferIndex, VkCommandBuffer commandBuffer, void* drawData,  graphics::IPipelineRef pipeline);

	private:
		// Delegate handle cache when swapchain rebuild.
		EventHandle m_afterSwapChainRebuildHandle;

		// Common buffers ring.
		std::vector<VkCommandBuffer> m_commandBuffers;
		struct RenderBuffers
		{
			graphics::HostVisibleGPUBufferRef verticesBuffer = nullptr;
			graphics::HostVisibleGPUBufferRef indicesBuffer  = nullptr;
		};
		std::vector<RenderBuffers> m_frameRenderBuffers;

		// UI backbuffer clear value.
		math::vec4 m_clearColor = { 0.45f, 0.55f, 0.60f, 1.00f };

		// UI ini layout config file store path.
		std::string m_iniFileStorePath;

		struct FontAtlas
		{
			float dpiScale = 0.0f;
			graphics::GPUTextureRef texture = nullptr;

			ImGuiStyle  style; // Style which scale by dpi.
			ImFontAtlas atlas; // Need to call clear when release.
		};
		std::map<uint32, FontAtlas> m_fontAtlasTextures;

		// Cache main atlas when init.
		ImFontAtlas* m_mainAtlas;
		ImGuiStyle   m_mainStyle;

		// Current active dpi scale.
		float m_dpiScale = 0.0f;
	};
}