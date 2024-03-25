#pragma once
#include <graphics/common.h>

namespace chord::graphics
{
	class Swapchain : NonCopyable
	{
	public:
		// Mainstream monitor support formats.
		static constexpr VkSurfaceFormatKHR k10BitSRGB = { .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		static constexpr VkSurfaceFormatKHR k8BitSRGB = { .format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		static constexpr VkSurfaceFormatKHR kScRGB = { .format = VK_FORMAT_R16G16B16A16_SFLOAT, .colorSpace = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT };
		static constexpr VkSurfaceFormatKHR kHDR10ST2084 = { .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT };

		// Format enum map.
		enum class EFormatType
		{
			None = 0,

			sRGB10Bit, // k10BitSRGB
			sRGB8Bit,  // k8BitSRGB
			scRGB,     // kScRGB
			ST2084,    // kHDR10ST2084

			MAX
		};

		struct SupportDetails
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> surfaceFormats;
			std::vector<VkPresentModeKHR> presentModes;
		};

	public:
		explicit Swapchain();
		~Swapchain();

		VkSwapchainKHR getSwapchain() const
		{
			return m_swapchain;
		}

		const VkExtent2D& getExtent() const
		{
			return m_extent;
		}

		EFormatType getFormatType() const
		{
			return m_formatType;
		}

		uint32 getBackbufferCount() const
		{
			return m_backbufferCount;
		}

		// Acquire next present image, return using image index.
		uint32 acquireNextPresentImage();

		// Mark swapchain dirty, so it will recreate context next time.
		// Call me when window cross multi monitor, resize, etc.
		void markDirty() 
		{ 
			m_bSwapchainChange = true; 
		}

		void submit(uint32 count, VkSubmitInfo* infos);
		void present();

		VkSemaphore getCurrentFrameWaitSemaphore() const;
		VkSemaphore getCurrentFrameFinishSemaphore() const;

		std::pair<VkCommandBuffer, VkSemaphore> beginFrameCmd(uint64 tickCount) const;

		// Call this event when swapchain prepare to recreate.
		Events<Swapchain> onBeforeSwapchainRecreate;

		// Call this event when window swapchain recreated.
		Events<Swapchain> onAfterSwapchainRecreate;

	private:
		void createContext();
		void releaseContext();

		// Context recreate.
		void recreateContext();

	private:
		// Working queue.
		VkQueue m_queue;

		// Vulkan surface.
		VkSurfaceKHR m_surface = VK_NULL_HANDLE;

		// Vulkan back buffer format type.
		EFormatType m_formatType = EFormatType::None;

		// Vulkan surface format.
		VkSurfaceFormatKHR m_surfaceFormat = {};

		// Current swapchain support detail.
		SupportDetails m_supportDetails;

		// Window swapchain.
		VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;

		// Own image.
		std::vector<VkImage> m_swapchainImages = {};

		// Image views.
		std::vector<VkImageView> m_swapchainImageViews = {};

		// Extent of swapchain.
		VkExtent2D m_extent = {};

		// Current swapchain present mode.
		VkPresentModeKHR m_presentMode = {};

		// Ring present realtive info.
		uint32 m_backbufferCount = 0;
		uint32 m_imageIndex = 0;
		uint32 m_currentFrame = 0;

		// Semaphores.
		std::vector<VkSemaphore> m_semaphoresImageAvailable;
		std::vector<VkSemaphore> m_semaphoresRenderFinished;

		// Fences.
		std::vector<VkFence> m_inFlightFences;
		std::vector<VkFence> m_imagesInFlight;

		// Swapchain dirty need rebuild context.
		bool m_bSwapchainChange = false;

		// Generic command buffer ring for renderer.
		std::vector<VkCommandBuffer> m_cmdBufferRing;

		// Semaphore wait for command buffer ring.
		std::vector<VkSemaphore> m_cmdSemaphoreRing; 
	};

}