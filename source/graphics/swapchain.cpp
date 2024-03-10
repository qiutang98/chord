#include <graphics/graphics.h>
#include <utils/cvar.h>
#include <application/application.h>
#include <graphics/helper.h>

namespace chord::graphics
{
	static AutoCVar<uint32> cVarDesiredSwapchainBackBufferCount(
		"r.graphics.swapchain.desiredBackBufferCount",
		3,
		"Desired swapchain back buffer count, default is 3.",
		EConsoleVarFlags::None
	);

	static AutoCVar<uint32> cVarDesiredSwapchainPresentMode(
		"r.graphics.swapchain.desiredPresentMode",
		0,
		"Desired swapchain present mode, 0 meaning use auto config, 1 is immediate, 2 is malibox, 3 is FIFO, 4 is FIFO relaxed.",
		EConsoleVarFlags::None
	);

	static inline VkPresentModeKHR getPresentMode(const std::vector<VkPresentModeKHR>& presentModes)
	{
		const auto type = cVarDesiredSwapchainPresentMode.get();
		auto isModeSupport = [&](VkPresentModeKHR mode)
		{
			for (const auto& availablePresentMode : presentModes)
			{
				if (availablePresentMode == mode)
				{
					return true;
				}
			}
			return false;
		};

		if (type == 1)
		{
			if (isModeSupport(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
		}
		else if (type == 2)
		{
			if (isModeSupport(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
		}
		else if (type == 4)
		{
			if (isModeSupport(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
		}

		if (isModeSupport(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
		if (isModeSupport(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
		if (isModeSupport(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;

		checkVkResult(isModeSupport(VK_PRESENT_MODE_FIFO_KHR));
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	Swapchain::Swapchain()
	{
		m_queue = getContext().getMajorGraphicsQueue();

		// Create surface.
		checkVkResult(glfwCreateWindowSurface(
			getContext().getInstance(),
			Application::get().getWindowData().window, 
			getContext().getAllocationCallbacks(), &m_surface));

		// Create swapchain relative context.
		createContext();
	}

	Swapchain::~Swapchain()
	{
		releaseContext();

		helper::destroySurface(m_surface);
	}

	uint32 Swapchain::acquireNextPresentImage()
	{
		// Flush fence.
		vkWaitForFences(getContext().getDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

		// Now acquire next image.
		VkResult result = vkAcquireNextImageKHR(
			getContext().getDevice(),
			m_swapchain,
			UINT64_MAX,
			m_semaphoresImageAvailable[m_currentFrame],
			VK_NULL_HANDLE,
			&m_imageIndex
		);

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			// If swapchain out of date then try rebuild context.
			recreateContext();
		}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			LOG_GRAPHICS_FATAL("Fail to requeset present image.");
		}

		// Wait same index in flight fence to ensure ring queue safe.
		if (m_imagesInFlight[m_imageIndex] != VK_NULL_HANDLE)
		{
			vkWaitForFences(getContext().getDevice(), 1, &m_imagesInFlight[m_imageIndex], VK_TRUE, UINT64_MAX);
		}

		// Update in flight fence.
		m_imagesInFlight[m_imageIndex] = m_inFlightFences[m_currentFrame];

		// Return current valid backbuffer index.
		return m_imageIndex;
	}

	void Swapchain::submit(uint32 count, VkSubmitInfo* infos)
	{
		// Reset fence before submit.
		checkVkResult(vkResetFences(getContext().getDevice(), 1, &m_inFlightFences[m_currentFrame]));

		// Submit with fence notify.
		checkVkResult(vkQueueSubmit(m_queue, count, infos, m_inFlightFences[m_currentFrame]));
	}

	void Swapchain::present()
	{
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		VkSemaphore signalSemaphores[] = { m_semaphoresRenderFinished[m_currentFrame] };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		VkSwapchainKHR swapchains[] = { m_swapchain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapchains;
		presentInfo.pImageIndices = &m_imageIndex;

		auto result = vkQueuePresentKHR(m_queue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_bSwapchainChange)
		{
			m_bSwapchainChange = false;
			recreateContext();
		}
		else if (result != VK_SUCCESS)
		{
			LOG_GRAPHICS_FATAL("Fail to present image.");
		}

		// if swapchain rebuild and on minimized, still add frame.
		m_currentFrame = (m_currentFrame + 1) % m_backbufferCount;
	}

	VkSemaphore Swapchain::getCurrentFrameWaitSemaphore() const
	{
		return m_semaphoresImageAvailable[m_currentFrame];
	}

	VkSemaphore Swapchain::getCurrentFrameFinishSemaphore() const
	{
		return m_semaphoresRenderFinished[m_currentFrame];
	}

	std::pair<VkCommandBuffer, VkSemaphore> Swapchain::beginFrameCmd(uint64 tickCount) const
	{
		const auto tickIndexModBackbufferCount = tickCount % m_backbufferCount;
		VkCommandBuffer cmd = m_cmdBufferRing[tickIndexModBackbufferCount];

		helper::resetCommandBuffer(cmd);
		helper::beginCommandBuffer(cmd);

		return { cmd, m_cmdSemaphoreRing[tickIndexModBackbufferCount]};
	}

	void Swapchain::createContext()
	{
		// Query current details.
		auto& caps = m_supportDetails.capabilities;
		{
			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(getContext().getPhysicalDevice(), m_surface, &caps);

			uint32 count;
			vkGetPhysicalDeviceSurfaceFormatsKHR(getContext().getPhysicalDevice(), m_surface, &count, nullptr);
			checkGraphics(count > 0);
			{
				m_supportDetails.surfaceFormats.resize(count);
				vkGetPhysicalDeviceSurfaceFormatsKHR(getContext().getPhysicalDevice(), m_surface, &count, m_supportDetails.surfaceFormats.data());
			}

			vkGetPhysicalDeviceSurfacePresentModesKHR(getContext().getPhysicalDevice(), m_surface, &count, nullptr);
			checkGraphics(count > 0);
			{
				m_supportDetails.presentModes.resize(count);
				vkGetPhysicalDeviceSurfacePresentModesKHR(getContext().getPhysicalDevice(), m_surface, &count, m_supportDetails.presentModes.data());
			}
		}

		// Choose swapchain present mode.
		m_presentMode = getPresentMode(m_supportDetails.presentModes);

		// Choose surface format.
		{
			auto isContainFormat = [&](VkSurfaceFormatKHR format)
			{
				for (const auto& availableFormat : m_supportDetails.surfaceFormats)
				{
					if (availableFormat.colorSpace == format.colorSpace && availableFormat.format == format.format)
					{
						return true;
					}
				}
				return false;
			};

			const VkSurfaceFormatKHR k10BitSRGB =
			{ .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
			const VkSurfaceFormatKHR k8BitSRGB =
			{ .format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
			const VkSurfaceFormatKHR kScRGB =
			{ .format = VK_FORMAT_R16G16B16A16_SFLOAT, .colorSpace = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT };
			const VkSurfaceFormatKHR kHDR10ST2084 =
			{ .format = VK_FORMAT_A2B10G10R10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT };

			if (getContext().isRequiredHDR())
			{
				if (isContainFormat(kHDR10ST2084))
				{
					m_formatType = EFormatType::ST2084;
					m_surfaceFormat = kHDR10ST2084;
				}
				else if (isContainFormat(kScRGB))
				{
					m_formatType = EFormatType::scRGB;
					m_surfaceFormat = kScRGB;
				}
			}

			if (m_formatType == EFormatType::None)
			{
				if (isContainFormat(k10BitSRGB))
				{
					m_formatType = EFormatType::sRGB10Bit;
					m_surfaceFormat = k10BitSRGB;
				}
				else
				{
					checkGraphics(isContainFormat(k8BitSRGB));
					m_formatType = EFormatType::sRGB8Bit;
					m_surfaceFormat = k8BitSRGB;
				}
			}
		}

		// Spec: currentExtent is the current width and height of the surface, or the special value (0xFFFFFFFF, 0xFFFFFFFF),
		//       indicating that the surface size will be determined by the extent of a swapchain targeting the surface.
		if (caps.currentExtent.width != 0xFFFFFFFF)
		{
			m_extent = caps.currentExtent;
		}
		else
		{
			int width, height;
			Application::get().queryFramebufferSize(width, height);

			VkExtent2D actualExtent = { static_cast<uint32>(width), static_cast<uint32>(height) };

			actualExtent.width = std::clamp(actualExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
			actualExtent.height = std::clamp(actualExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);

			m_extent = actualExtent;
		}

		m_backbufferCount = std::clamp(cVarDesiredSwapchainBackBufferCount.get(), caps.minImageCount, caps.maxImageCount);
		m_swapchain = helper::createSwapchain(m_surface, m_backbufferCount, m_surfaceFormat, m_extent, caps.currentTransform, m_presentMode);

		// Get swapchain images.
		m_swapchainImages.resize(m_backbufferCount);
		vkGetSwapchainImagesKHR(getContext().getDevice(), m_swapchain, &m_backbufferCount, m_swapchainImages.data());

		// Create image views.
		m_swapchainImageViews.resize(m_backbufferCount);
		for (auto i = 0; i < m_swapchainImages.size(); i++)
		{
			VkImageViewCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.pNext = nullptr;
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.image = m_swapchainImages[i];
			createInfo.format = m_surfaceFormat.format;
			createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.baseMipLevel = 0;
			createInfo.subresourceRange.levelCount = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount = 1;

			// Create swapchain image view.
			m_swapchainImageViews[i] = helper::createImageView(createInfo);
		}

		// Init present relative context.
		{
			m_semaphoresImageAvailable.resize(m_backbufferCount);
			m_semaphoresRenderFinished.resize(m_backbufferCount);

			m_inFlightFences.resize(m_backbufferCount);
			m_imagesInFlight.resize(m_backbufferCount);

			for (auto& fence : m_imagesInFlight)
			{
				fence = VK_NULL_HANDLE;
			}

			for (auto i = 0; i < m_backbufferCount; i++)
			{
				m_semaphoresImageAvailable[i] = helper::createSemaphore();
				m_semaphoresRenderFinished[i] = helper::createSemaphore();

				m_inFlightFences[i] = helper::createFence(VK_FENCE_CREATE_SIGNALED_BIT);
			}
		}

		{
			m_cmdPool.family = getContext().getQueuesInfo().graphicsFamily.get();
			m_cmdPool.queue  = getContext().getQueuesInfo().graphcisQueues[0].queue;
			m_cmdPool.pool = helper::createCommandPool(m_cmdPool.family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

			m_cmdBufferRing.resize(m_backbufferCount);
			for (auto& buffer : m_cmdBufferRing)
			{
				buffer = helper::allocateCommandBuffer(m_cmdPool.pool);
			}

			m_cmdSemaphoreRing.resize(m_backbufferCount);
			for (auto& semaphore : m_cmdSemaphoreRing)
			{
				// Default signaled.
				semaphore = helper::createSemaphore(VK_FENCE_CREATE_SIGNALED_BIT);
			}
		}
	}

	void Swapchain::releaseContext()
	{
		// Flush queue.
		vkQueueWaitIdle(m_queue);

		for (auto& buffer : m_cmdBufferRing)
		{
			vkFreeCommandBuffers(getContext().getDevice(), m_cmdPool.pool, (uint32)m_cmdBufferRing.size(), m_cmdBufferRing.data());
		}
		m_cmdBufferRing.clear();

		for (auto& semaphore : m_cmdSemaphoreRing)
		{
			helper::destroySemaphore(semaphore);
		}
		m_cmdSemaphoreRing.clear();

		helper::destroyCommandPool(m_cmdPool.pool);

		// Destroy present relative data.
		for (auto i = 0; i < m_backbufferCount; i++)
		{
			helper::destroySemaphore(m_semaphoresImageAvailable[i]);
			helper::destroySemaphore(m_semaphoresRenderFinished[i]);

			helper::destroyFence(m_inFlightFences[i]);
		}

		for (auto& imageView : m_swapchainImageViews)
		{
			helper::destroyImageView(imageView);
		}

		helper::destroySwapchain(m_swapchain);

		// Release tick index.
		m_backbufferCount = 0;
		m_imageIndex = 0;
		m_currentFrame = 0;
	}

	void Swapchain::recreateContext()
	{
		// Need to skip zero size framebuffer case.
		int32 width = 0, height = 0;
		Application::get().queryFramebufferSize(width, height);

		// just return if swapchain width or height is 0.
		if (width == 0 || height == 0)
		{
			m_bSwapchainChange = true;
			return;
		}

		onBeforeSwapchainRecreate.broadcast();
		{
			// Context rebuild.
			releaseContext();
			createContext();
		}
		onAfterSwapchainRecreate.broadcast();
	}
}