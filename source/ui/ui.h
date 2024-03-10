#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/delegate.h>

namespace chord
{
	class UIManager : NonCopyable
	{
	public:

		void init();

		void newFrame();
		void render();

		void release();

	private:
		void updateStyle();


	private:
		// Delegate handle cache when swapchain rebuild.
		EventHandle m_beforeSwapChainRebuildHandle;
		EventHandle m_afterSwapChainRebuildHandle;

		struct ImguiPassGpuResource
		{
			VkDescriptorPool descriptorPool;
			VkRenderPass renderPass = VK_NULL_HANDLE;

			std::vector<VkFramebuffer>   framebuffers;
			std::vector<VkCommandPool>   commandPools;
			std::vector<VkCommandBuffer> commandBuffers;
		} m_resources;

		// UI backbuffer clear value.
		math::vec4 m_clearColor = { 0.45f, 0.55f, 0.60f, 1.00f };
	};
}