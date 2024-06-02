#include <graphics/swapchain.h>
#include <graphics/graphics.h>
#include <utils/cvar.h>
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

	Swapchain::Swapchain(GLFWwindow* window)
		: m_window(window)
	{
		// NOTE: Current default work in main graphics queue.
		m_queue = getContext().getMajorGraphicsQueue();

		// Create surface.
		checkVkResult(glfwCreateWindowSurface(
			getContext().getInstance(),
			window,
			getContext().getAllocationCallbacks(), &m_surface));

		m_commandList = std::make_unique<CommandList>();

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

		// When fence finish, can free pending resource.
		m_pendingResources[m_currentFrame].clear();

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

		// Signal command list when new frame required.
		m_commandList->sync();

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

			// HDR first require.
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

			// Fallback to SDR.
			if (m_formatType == EFormatType::None)
			{
				if (isContainFormat(k10BitSRGB) && false)
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
			glfwGetFramebufferSize(m_window, &width, &height);

			VkExtent2D actualExtent = { static_cast<uint32>(width), static_cast<uint32>(height) };

			actualExtent.width = std::clamp(actualExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
			actualExtent.height = std::clamp(actualExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);

			m_extent = actualExtent;
		}

		// Backbuffer count clamp by hardware.
		m_backbufferCount = std::clamp(cVarDesiredSwapchainBackBufferCount.get(), caps.minImageCount, caps.maxImageCount);
		m_pendingResources.resize(m_backbufferCount);

		// Now create swapchain.
		m_swapchain = helper::createSwapchain(m_surface, m_backbufferCount, m_surfaceFormat, m_extent, caps.currentTransform, m_presentMode);

		// Get swapchain images.
		{
			m_swapchainImages.resize(m_backbufferCount);
			vkGetSwapchainImagesKHR(getContext().getDevice(), m_swapchain, &m_backbufferCount, m_swapchainImages.data());
		}

		// Create image views.
		m_swapchainImageViews.resize(m_backbufferCount);
		for (auto i = 0; i < m_swapchainImages.size(); i++)
		{
			VkImageViewCreateInfo createInfo{};
			createInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.pNext    = nullptr;
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.image    = m_swapchainImages[i];
			createInfo.format   = m_surfaceFormat.format;

			// Current don't need swizzle.
			createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

			// Color bit usage.
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

			// Basic subresource.
			createInfo.subresourceRange.baseMipLevel   = 0;
			createInfo.subresourceRange.levelCount     = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount     = 1;

			// Create swapchain image view.
			m_swapchainImageViews[i] = helper::createImageView(createInfo);
		}

		// Init present relative context.
		{
			m_imagesInFlight.resize(m_backbufferCount);
			for (auto& fence : m_imagesInFlight)
			{
				fence = VK_NULL_HANDLE;
			}

			m_semaphoresImageAvailable.resize(m_backbufferCount);
			m_semaphoresRenderFinished.resize(m_backbufferCount);
			m_inFlightFences.resize(m_backbufferCount);
			for (auto i = 0; i < m_backbufferCount; i++)
			{
				m_semaphoresImageAvailable[i] = helper::createSemaphore();
				m_semaphoresRenderFinished[i] = helper::createSemaphore();

				m_inFlightFences[i] = helper::createFence(VK_FENCE_CREATE_SIGNALED_BIT);
			}
		}
	}

	void Swapchain::releaseContext()
	{
		// Ensure all in flight images ready.
		for(auto fence : m_imagesInFlight)
		{
			if (fence != VK_NULL_HANDLE)
			{
				vkWaitForFences(getContext().getDevice(), 1, &fence, VK_TRUE, UINT64_MAX);
			}
		}

		// Release all pending resources.
		m_pendingResources.clear();

		// Destroy present relative data.
		for (auto i = 0; i < m_backbufferCount; i++)
		{
			helper::destroySemaphore(m_semaphoresImageAvailable[i]);
			helper::destroySemaphore(m_semaphoresRenderFinished[i]);

			// Fence destroy.
			helper::destroyFence(m_inFlightFences[i]);
		}

		// Destroy swapchain cache image views.
		for (auto& imageView : m_swapchainImageViews)
		{
			helper::destroyImageView(imageView);
		}

		helper::destroySwapchain(m_swapchain);

		// Release tick index.
		m_backbufferCount = 0;
		m_imageIndex      = 0;
		m_currentFrame    = 0;
	}

	void Swapchain::recreateContext()
	{
		// Need to skip zero size framebuffer case.
		{
			int32 width = 0, height = 0;
			glfwGetFramebufferSize(m_window, &width, &height);

			// just return if swapchain width or height is 0.
			if (width == 0 || height == 0)
			{
				// Need to mark swapchain change state.
				// So next time when framebuffer resize we stil know we need to recreate.
				m_bSwapchainChange = true;
				return;
			}
		}

		// Now recreate context.
		onBeforeSwapchainRecreate.broadcast();
		{
			releaseContext();
			createContext();
		}
		onAfterSwapchainRecreate.broadcast();
	}
}