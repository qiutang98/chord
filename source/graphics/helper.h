#pragma once

#include <graphics/graphics.h>

namespace chord::graphics::helper
{
	

	static inline VkImageSubresourceRange buildBasicImageSubresource(VkImageAspectFlags flags = VK_IMAGE_ASPECT_COLOR_BIT)
	{
		VkImageSubresourceRange range { };
		range.aspectMask = flags;
		range.baseMipLevel = 0;
		range.levelCount = VK_REMAINING_MIP_LEVELS;
		range.baseArrayLayer = 0;
		range.layerCount = VK_REMAINING_ARRAY_LAYERS;
		return range;
	}

	static inline VkImageCreateInfo buildBasicUploadImageCreateInfo(uint32 texWidth, uint32 texHeight, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM)
	{
		VkImageCreateInfo info{};
		info.sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		info.flags     = {};
		info.imageType = VK_IMAGE_TYPE_2D;
		info.format    = format;
		info.extent.width  = texWidth;
		info.extent.height = texHeight;
		info.extent.depth  = 1;
		info.arrayLayers = 1;
		info.mipLevels   = 1;
		info.samples = VK_SAMPLE_COUNT_1_BIT;
		info.tiling  = VK_IMAGE_TILING_OPTIMAL;
		info.usage   = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
		info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		return info;
	}

	static inline VmaAllocationCreateInfo buildVMAUploadImageAllocationCI()
	{
		VmaAllocationCreateInfo imageAllocCreateInfo = {};
		imageAllocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
		imageAllocCreateInfo.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;

		return imageAllocCreateInfo;
	}

	static inline auto createDescriptorPool(const VkDescriptorPoolCreateInfo& poolInfo)
	{
		VkDescriptorPool descriptorPool;
		checkVkResult(vkCreateDescriptorPool(getDevice(), &poolInfo, getAllocationCallbacks(), &descriptorPool));
		return descriptorPool;
	}

	static inline auto createDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo& info)
	{
		VkDescriptorSetLayout layout;
		checkVkResult(vkCreateDescriptorSetLayout(getDevice(), &info, getAllocationCallbacks(), &layout));
		return layout;
	}

	static inline auto createTimelineSemaphore(uint32 initialValue = 0)
	{
		VkSemaphoreTypeCreateInfo timelineCI{ };
		timelineCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		timelineCI.pNext = nullptr;
		timelineCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		timelineCI.initialValue = initialValue;

		VkSemaphoreCreateInfo CI{ };
		CI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		CI.pNext = &timelineCI;
		CI.flags = 0;

		VkSemaphore timelineSemaphore;
		checkVkResult(vkCreateSemaphore(getDevice(), &CI, getAllocationCallbacks(), &timelineSemaphore));
		return timelineSemaphore;
	}

	static inline auto createCommandPool(
		uint32 family,
		VkCommandPoolCreateFlags flags = 
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | 
			VK_COMMAND_POOL_CREATE_TRANSIENT_BIT)
	{
		VkCommandPoolCreateInfo ci{ };
		ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		ci.pNext = nullptr;
		ci.queueFamilyIndex = family;
		ci.flags = flags;

		VkCommandPool pool;
		checkVkResult(vkCreateCommandPool(getDevice(), &ci, getAllocationCallbacks(), &pool));

		return pool;
	}

	static inline auto allocateCommandBuffer(
		VkCommandPool pool,
		VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY)
	{
		VkCommandBufferAllocateInfo ai{};
		ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		ai.pNext = nullptr;
		ai.level = level;
		ai.commandBufferCount = 1;
		ai.commandPool = pool;

		VkCommandBuffer cmd;
		checkVkResult(vkAllocateCommandBuffers(getDevice(), &ai, &cmd));

		return cmd;
	}

	static inline auto createSwapchain(
		VkSurfaceKHR surface,
		uint32 imageCount,
		const VkSurfaceFormatKHR& format,
		const VkExtent2D& extent,
		VkSurfaceTransformFlagBitsKHR preTransform,
		VkPresentModeKHR presentMode)
	{
		VkSwapchainCreateInfoKHR createInfo{ };
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.pNext = nullptr;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = format.format;
		createInfo.imageColorSpace = format.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;

		createInfo.imageUsage =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		// Only use graphics family to draw and present.
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;

		createInfo.preTransform = preTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;

		VkSwapchainKHR swapchain;
		checkVkResult(vkCreateSwapchainKHR(getDevice(), &createInfo, getAllocationCallbacks(), &swapchain));

		return swapchain;
	}

	static inline auto createSemaphore(VkSemaphoreCreateFlags flags = 0)
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		semaphoreInfo.flags = 0;

		VkSemaphore semaphore;
		checkVkResult(vkCreateSemaphore(getDevice(), &semaphoreInfo, getAllocationCallbacks(), &semaphore));

		return semaphore;
	}

	static inline auto createFence(VkFenceCreateFlags flags)
	{
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = flags;

		VkFence fence;
		checkVkResult(vkCreateFence(getDevice(), &fenceInfo, getAllocationCallbacks(), &fence));

		return fence;
	}

	static inline auto destroySwapchain(VkSwapchainKHR& swapchain)
	{
		if (swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(getDevice(), swapchain, getAllocationCallbacks());
			swapchain = VK_NULL_HANDLE;
		}
	}

	static inline void destroySurface(VkSurfaceKHR& surface)
	{
		if (surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(getContext().getInstance(), surface, getAllocationCallbacks());
			surface = VK_NULL_HANDLE;
		}
	}

	static inline void destroyCommandPool(VkCommandPool& pool)
	{
		if (pool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(getDevice(), pool, getAllocationCallbacks());
			pool = VK_NULL_HANDLE;
		}
	}

	static inline void destroyPipelineCache(VkPipelineCache& cache)
	{
		if (cache != VK_NULL_HANDLE)
		{
			vkDestroyPipelineCache(getDevice(), cache, getAllocationCallbacks());
			cache = VK_NULL_HANDLE;
		}
	}

	static inline void destroyDebugUtilsMessenger(VkDebugUtilsMessengerEXT& o)
	{
		if (o != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(getContext().getInstance(), o, getAllocationCallbacks());
			o = VK_NULL_HANDLE;
		}
	}

	static inline auto createImageView(const VkImageViewCreateInfo& ci)
	{
		VkImageView view;
		checkVkResult(vkCreateImageView(getDevice(), &ci, getAllocationCallbacks(), &view));

		return view;
	}

	static inline void destroyImageView(VkImageView& view)
	{
		if (view != VK_NULL_HANDLE)
		{
			vkDestroyImageView(getDevice(), view, getAllocationCallbacks());
			view = VK_NULL_HANDLE;
		}
	}

	static inline VkVertexInputBindingDescription2EXT vertexInputBindingDescription2EXT(
		uint32 stride,
		uint32 binding = 0,
		VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		uint32 divisor = 1)
	{
		VkVertexInputBindingDescription2EXT ext { };
		ext.sType     = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
		ext.binding   = binding;
		ext.stride    = stride;
		ext.inputRate = inputRate;
		ext.divisor   = divisor;
		return ext;
	}

	static inline VkVertexInputAttributeDescription2EXT vertexInputAttributeDescription2EXT(
		uint32 location,
		VkFormat format,
		uint32 offset,
		uint32 binding = 0)
	{
		VkVertexInputAttributeDescription2EXT ext{};
		ext.sType    = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
		ext.binding  = binding;
		ext.location = location;
		ext.format   = format;
		ext.offset   = offset;
		return ext;
	}

	static inline void setViewport(VkCommandBuffer cmd, int32 width, int32 height)
	{
		VkViewport viewport;
		viewport.x = 0;
		viewport.y = 0;

		viewport.width  = (float)width;
		viewport.height = (float)height;

		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewportWithCount(cmd, 1, &viewport);
	}

	static inline void setScissor(VkCommandBuffer cmd, const VkOffset2D& offset, const VkExtent2D& extent)
	{
		const VkRect2D scissor { .offset = offset, .extent = extent };
		vkCmdSetScissorWithCount(cmd, 1, &scissor);
	}

	static inline VkRenderingAttachmentInfo renderingAttachmentInfo(bool bDepth)
	{
		VkRenderingAttachmentInfo attachment{ };

		attachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		attachment.imageLayout = bDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		return attachment;
	}

	static inline VkRenderingInfo renderingInfo()
	{
		VkRenderingInfo renderInfo { };
		renderInfo.sType      = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
		renderInfo.layerCount = 1;

		return renderInfo;
	}

	static inline void dynamicStateGeneralSet(VkCommandBuffer commandBuffer)
	{
		vkCmdSetCullMode(commandBuffer,                VK_CULL_MODE_NONE);
		vkCmdSetPolygonModeEXT(commandBuffer,          VK_POLYGON_MODE_FILL);
		vkCmdSetRasterizationSamplesEXT(commandBuffer, VK_SAMPLE_COUNT_1_BIT);
		vkCmdSetFrontFace(commandBuffer,               VK_FRONT_FACE_COUNTER_CLOCKWISE);

		vkCmdSetDepthTestEnable(commandBuffer,       VK_FALSE);
		vkCmdSetDepthWriteEnable(commandBuffer,      VK_FALSE);
		vkCmdSetDepthBoundsTestEnable(commandBuffer, VK_FALSE);
		vkCmdSetDepthBiasEnable(commandBuffer,       VK_FALSE);
		vkCmdSetDepthClampEnableEXT(commandBuffer,   VK_FALSE);
		vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_ALWAYS);

		vkCmdSetStencilTestEnable(commandBuffer,     VK_FALSE);
		vkCmdSetStencilOp(commandBuffer,
			VK_STENCIL_FACE_FRONT_AND_BACK,
			VK_STENCIL_OP_KEEP,
			VK_STENCIL_OP_KEEP,
			VK_STENCIL_OP_KEEP,
			VK_COMPARE_OP_ALWAYS);

		vkCmdSetLogicOpEnableEXT(commandBuffer,      VK_FALSE);
		vkCmdSetLogicOpEXT(commandBuffer, VK_LOGIC_OP_NO_OP);

		VkColorComponentFlags colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT;
		vkCmdSetColorWriteMaskEXT(commandBuffer, 0, 1, &colorWriteMask);
	}

	static inline void destroySemaphore(VkSemaphore& s)
	{
		if (s != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(getDevice(), s, getAllocationCallbacks());
			s = VK_NULL_HANDLE;
		}
	}

	static inline void destroyFence(VkFence& s)
	{
		if (s != VK_NULL_HANDLE)
		{
			vkDestroyFence(getDevice(), s, getAllocationCallbacks());
			s = VK_NULL_HANDLE;
		}
	}

	static inline void destroyShaderModule(VkShaderModule& m)
	{
		if (m != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(getDevice(), m, getAllocationCallbacks());
			m = VK_NULL_HANDLE;
		}
	}

	static inline void destroyDescriptorPool(VkDescriptorPool& pool)
	{
		if (pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(getDevice(), pool, getAllocationCallbacks());
			pool = VK_NULL_HANDLE;
		}
	}

	static inline void destroyDescriptorSetLayout(VkDescriptorSetLayout& l)
	{
		if (l != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(getDevice(), l, getAllocationCallbacks());
			l = VK_NULL_HANDLE;
		}
	}

	static inline void resetCommandBuffer(VkCommandBuffer cmd, VkCommandBufferResetFlags flags = 0)
	{
		checkVkResult(vkResetCommandBuffer(cmd, flags));
	}

	static inline void beginCommandBuffer(VkCommandBuffer cmd, VkCommandBufferUsageFlags flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
	{
		VkCommandBufferBeginInfo info = { };

		info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		info.pNext = nullptr;
		info.pInheritanceInfo = nullptr;
		info.flags = flags;

		checkVkResult(vkBeginCommandBuffer(cmd, &info));
	}

	static inline void endCommandBuffer(VkCommandBuffer cmd)
	{
		checkVkResult(vkEndCommandBuffer(cmd));
	}

	class SubmitInfo
	{
	private:
		VkSubmitInfo m_submitInfo{ };
		static const std::array<VkPipelineStageFlags, 1> kDefaultWaitStages;

	public:
		auto& clear()
		{
			m_submitInfo = { };
			m_submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			m_submitInfo.pWaitDstStageMask = kDefaultWaitStages.data();
			return *this;
		}

		SubmitInfo()
		{
			clear();
		}

		operator VkSubmitInfo()
		{
			return m_submitInfo;
		}

		auto& get() { return m_submitInfo; }
		const auto& get() const { return m_submitInfo; }

		auto& setWaitStage(VkPipelineStageFlags* waitStages)
		{
			m_submitInfo.pWaitDstStageMask = waitStages;
			return *this;
		}

		auto& setWaitStage(std::vector<VkPipelineStageFlags>&& waitStages) = delete;

		auto& setWaitSemaphore(VkSemaphore* wait, int32_t count)
		{
			m_submitInfo.waitSemaphoreCount = count;
			m_submitInfo.pWaitSemaphores = wait;
			return *this;
		}

		auto& setSignalSemaphore(VkSemaphore* signal, int32_t count)
		{
			m_submitInfo.signalSemaphoreCount = count;
			m_submitInfo.pSignalSemaphores = signal;
			return *this;
		}

		auto& setCommandBuffer(VkCommandBuffer* cb, int32_t count)
		{
			m_submitInfo.commandBufferCount = count;
			m_submitInfo.pCommandBuffers = cb;
			return *this;
		}
	};

}