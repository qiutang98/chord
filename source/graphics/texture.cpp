#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/bindless.h>
#include <application/application.h>
#include <utils/engine.h>
#include <utils/cityhash.h>

namespace chord::graphics
{
	static bool bGraphicsTextureLifeLogTraceEnable = true;
	static AutoCVarRef<bool> cVarTextureLifeLogTraceEnable(
		"r.graphics.resource.texture.lifeLogTrace",
		bGraphicsTextureLifeLogTraceEnable,
		"Enable log trace for texture create/destroy or not.",
		EConsoleVarFlags::ReadOnly
	);

	// Global device size for GPUTexture.
	static VkDeviceSize sTotalGPUTextureDeviceSize = 0;

	const VkImageSubresourceRange graphics::kDefaultImageSubresourceRange = helper::buildBasicImageSubresource();

	GPUTexture::GPUTexture(
		const std::string& name,
		const VkImageCreateInfo& createInfo,
		const VmaAllocationCreateInfo& vmaCreateInfo)
		: GPUResource(name, 0)
		, m_createInfo(createInfo)
	{
		VmaAllocationCreateInfo copyVMAInfo = vmaCreateInfo;
		copyVMAInfo.pUserData = (void*)getName().c_str();

		checkVkResult(vmaCreateImage(getVMA(), &createInfo, &vmaCreateInfo, &m_image, &m_allocation, &m_vmaAllocationInfo));

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(getDevice(), m_image, &memRequirements);

		// Use actually memory size.
		GPUResource::setSize(memRequirements.size);
		rename(name, true);

		sTotalGPUTextureDeviceSize += getSize();
		if (bGraphicsTextureLifeLogTraceEnable)
		{
			LOG_GRAPHICS_TRACE("Create GPUTexture {0} with size {1} KB.", getName(), float(getSize()) / 1024.0f)
		}

		// Init some subresources.
		auto subresourceNum = m_createInfo.arrayLayers * m_createInfo.mipLevels;
		m_subresourceStates.resize(subresourceNum);
		for (auto& state : m_subresourceStates)
		{
			// Prepare initial layout.
			state.imageLayout = createInfo.initialLayout;
		}
	}

	GPUTexture::~GPUTexture()
	{
		// Application releasing state guide us use a update or not in blindess free.
		const bool bAppReleasing = (Application::get().getRuntimePeriod() == ERuntimePeriod::Releasing);

		if (bGraphicsTextureLifeLogTraceEnable)
		{
			LOG_GRAPHICS_TRACE("Destroy GPUTexture {0} with size {1} KB.", getName(), float(getSize()) / 1024.0f)
		}
		sTotalGPUTextureDeviceSize -= getSize();

		ImageView fallbackView = bAppReleasing ? ImageView{ } :
			getContext().getBuiltinResources().white->getOwnHandle()->requireView(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D, false, false);

		// Destroy cached image views.
		for (auto& pair : m_views)
		{
			auto& view = pair.second;
			if (view.handle.isValid())
			{
				if (view.SRV.isValid())
				{
					getBindless().freeSRV(view.SRV, fallbackView);
				}

				if (view.UAV.isValid())
				{
					getBindless().freeUAV(view.UAV, fallbackView);
				}

				vkDestroyImageView(getDevice(), view.handle.get(), getAllocationCallbacks());
			}
		}

		if (m_image != VK_NULL_HANDLE)
		{
			vmaDestroyImage(getVMA(), m_image, m_allocation);
			m_image = VK_NULL_HANDLE;
		}
	}

	void GPUTexture::rename(const std::string& name, bool bForce)
	{
		if (setName(name, bForce))
		{
			setResourceName(VK_OBJECT_TYPE_IMAGE, (uint64)m_image, getName().c_str());
		}
	}

	// NOTE: Performance optimization guide.
	//       This is a general safe access flags compute function, no for best performance.
	// 
	CHORD_DEPRECATED("Performance poor function, don't use it.")
		static void getVkAccessFlagsByLayout(VkImageLayout oldLayout, VkImageLayout newLayout, VkAccessFlags& srcMask, VkAccessFlags& dstMask)
	{
		switch (oldLayout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:                        srcMask = 0; break;
		case VK_IMAGE_LAYOUT_PREINITIALIZED:                   srcMask = VK_ACCESS_HOST_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:         srcMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: srcMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             srcMask = VK_ACCESS_TRANSFER_READ_BIT; break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             srcMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:         srcMask = VK_ACCESS_SHADER_READ_BIT; break;
		case VK_IMAGE_LAYOUT_GENERAL:                          srcMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; break;

		default: LOG_GRAPHICS_FATAL("Image src layout '{0}' transition no support.", nameof::nameof_enum(oldLayout));
		}

		switch (newLayout)
		{
		case VK_IMAGE_LAYOUT_GENERAL:                          dstMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:             dstMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:             dstMask = VK_ACCESS_TRANSFER_READ_BIT; break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:         dstMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: dstMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		{
			if (srcMask == 0)
			{
				srcMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			}
			dstMask = VK_ACCESS_SHADER_READ_BIT;
		}
		break;
		default: LOG_GRAPHICS_FATAL("Image dest layout '{0}' transition no support.", nameof::nameof_enum(newLayout));
		}
	}

	// Generic pipeline state, so don't call this in render pipe, just for queue transfer purpose.
	void GPUTexture::transition(VkCommandBuffer cb, const GPUTextureSyncBarrierMasks& newState, const VkImageSubresourceRange& inRange)
	{
		std::vector<VkImageMemoryBarrier> barriers;

		VkDependencyFlags dependencyFlags{ };
		auto range = inRange;

		// Generic pipeline state, so don't call this in render pipe, just for queue transfer purpose.
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		uint32 maxLayer = math::min(range.baseArrayLayer + range.layerCount, m_createInfo.arrayLayers);
		for (uint32 layerIndex = range.baseArrayLayer; layerIndex < maxLayer; layerIndex++)
		{
			uint32 maxMip = math::min(range.baseMipLevel + range.levelCount, m_createInfo.mipLevels);
			for (uint32 mipIndex = range.baseMipLevel; mipIndex < maxMip; mipIndex++)
			{
				uint32 flatId = getSubresourceIndex(layerIndex, mipIndex);

				const auto& oldState = m_subresourceStates.at(flatId);

				// State no change.
				if (newState == oldState)
				{
					continue;
				}

				VkImageMemoryBarrier barrier{};
				barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				barrier.oldLayout = oldState.imageLayout;
				barrier.newLayout = newState.imageLayout;
				barrier.srcQueueFamilyIndex = (oldState.barrierMasks.queueFamilyIndex == VK_QUEUE_FAMILY_IGNORED) 
					? newState.barrierMasks.queueFamilyIndex
					: oldState.barrierMasks.queueFamilyIndex;
				barrier.dstQueueFamilyIndex = newState.barrierMasks.queueFamilyIndex;
				barrier.image = m_image;

				// Build subresourceRange
				VkImageSubresourceRange rangSpecial
				{
					.aspectMask = range.aspectMask,
					.baseMipLevel = mipIndex,
					.levelCount = 1,
					.baseArrayLayer = layerIndex,
					.layerCount = 1,
				};
				barrier.subresourceRange = rangSpecial;

				barrier.srcAccessMask = oldState.barrierMasks.accesMask;
				barrier.dstAccessMask = newState.barrierMasks.accesMask;
				barriers.push_back(barrier);

				// Update state.
				m_subresourceStates[flatId] = newState;
			}
		}

		// Skip if no barriers exist.
		if (barriers.empty())
		{
			return;
		}

		vkCmdPipelineBarrier(cb, srcStageMask, dstStageMask, dependencyFlags, 0, nullptr, 0, nullptr, (uint32_t)barriers.size(), barriers.data());
	}

	void GPUTexture::transitionImmediately(VkImageLayout newImageLayout, const VkImageSubresourceRange& range)
	{
		getContext().executeImmediatelyMajorGraphics([&](VkCommandBuffer cmd, uint32 family, VkQueue queue)
		{
			GPUTextureSyncBarrierMasks newState;
			newState.imageLayout = newImageLayout;
			newState.barrierMasks.queueFamilyIndex = family;
			newState.barrierMasks.accesMask = VK_ACCESS_NONE;

			transition(cmd, newState, range);
		});
	}

	uint32 GPUTexture::getSubresourceIndex(uint32 layerIndex, uint32 mipLevel) const
	{
		checkGraphics((layerIndex < m_createInfo.arrayLayers) && (mipLevel < m_createInfo.mipLevels));
		return layerIndex * m_createInfo.mipLevels + mipLevel;
	}

	ImageView GPUTexture::requireView(const VkImageSubresourceRange& range, VkImageViewType viewType, bool bSRV, bool bUAV)
	{
		VkImageViewCreateInfo info = { };
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.pNext = nullptr;
		info.flags = 0;
		info.image = m_image;
		info.viewType = viewType;
		info.format = m_createInfo.format;
		info.subresourceRange = range;

		// Create image view if no init.
		auto& view = m_views[cityhash::cityhash64((const char*)(&info), sizeof(info))];
		if (!view.handle.isValid())
		{
			view.handle = helper::createImageView(info);
			setResourceName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64)view.handle.get(), getName().c_str());
		}

		// Create SRV if require.
		if (bSRV && (!view.SRV.isValid()))
		{
			view.SRV = getBindless().registerSRV(view.handle.get());
		}

		// Create UAV if require.
		if (bUAV && (!view.UAV.isValid()))
		{
			view.UAV = getBindless().registerUAV(view.handle.get());
		}

		return view;
	}
}