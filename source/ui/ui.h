#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/delegate.h>
#include <graphics/resource.h>

namespace chord
{
	class UIManager : NonCopyable
	{
	public:
		void init();
		void release();

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

		// Get command buffer in frame index.
		VkCommandBuffer getCommandBuffer(uint32 index) const
		{
			return m_commandBuffers.at(index);
		}

	private:
		// Setup font relative resource.
		void setupFont();

		// Update ui style.
		void updateStyle();

		// Imgui render pass build and release.
		void buildRenderResource();

	private:
		// Delegate handle cache when swapchain rebuild.
		EventHandle m_afterSwapChainRebuildHandle;

		// Common buffers ring.
		std::vector<VkCommandBuffer> m_commandBuffers;

		// UI backbuffer clear value.
		math::vec4 m_clearColor = { 0.45f, 0.55f, 0.60f, 1.00f };

		// Font texture rasterize with base size.
		graphics::GPUTextureRef m_fontAtlasTexture = nullptr;
	};
}