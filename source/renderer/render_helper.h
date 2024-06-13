#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/bufferpool.h>

namespace chord
{
	struct DepthStencilRT
	{
		graphics::PoolTextureRef RT = nullptr;
		graphics::EDepthStencilOp depthStencilOp = graphics::EDepthStencilOp::DepthNop_StencilNop;
		graphics::ERenderTargetLoadStoreOp Op = graphics::ERenderTargetLoadStoreOp::Nope_Nope;
		VkClearValue clearValue;
	};

	struct RenderTargetRT
	{
		RenderTargetRT() = default;

		RenderTargetRT(graphics::PoolTextureRef inRT, graphics::ERenderTargetLoadStoreOp inOp, VkClearValue inClearValue = {})
			: RT(inRT)
			, Op(inOp)
			, clearValue(inClearValue)
		{

		}

		graphics::PoolTextureRef RT = nullptr;
		graphics::ERenderTargetLoadStoreOp Op = graphics::ERenderTargetLoadStoreOp::Nope_Nope;
		VkClearValue clearValue;
	};

	class RenderTargets
	{
	public:
		RenderTargets()
		{
			for (auto& bEnableBlend : bEnableBlends)
			{
				bEnableBlend = VK_FALSE;
			}
			for (auto& ext : colorBlendEquations)
			{
				ext.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				ext.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				ext.colorBlendOp = VK_BLEND_OP_ADD;
				ext.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				ext.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				ext.alphaBlendOp = VK_BLEND_OP_ADD;
			}
		}

		RenderTargetRT RTs[graphics::kMaxRenderTargets];
		VkBool32 bEnableBlends[graphics::kMaxRenderTargets];
		VkColorBlendEquationEXT colorBlendEquations[graphics::kMaxRenderTargets];

		DepthStencilRT depthStencil;

		std::vector<VkFormat> getRTsFormats() const
		{
			std::vector<VkFormat> result { };
			for (auto& RT : RTs)
			{
				if(RT.RT == nullptr) { break; }
				result.push_back(RT.RT->get()->getFormat());
			}
			return std::move(result);
		}

		VkFormat getDepthStencilFormat() const
		{
			if(depthStencil.RT == nullptr) return VK_FORMAT_UNDEFINED;
			return depthStencil.RT->get()->getFormat();
		}

		void beginRendering(graphics::GraphicsQueue& queue) const
		{
			using namespace graphics;
			queue.checkRecording();

			transitionForRender(queue);

			uint32 width  = ~0;
			uint32 height = ~0;

			const bool bDepthStencilValue = depthStencil.RT != nullptr;
			VkRenderingAttachmentInfo depthAttachmentInfo = graphics::helper::renderingAttachmentInfo(true);
			if (bDepthStencilValue)
			{
				width = depthStencil.RT->get()->getExtent().width;
				height = depthStencil.RT->get()->getExtent().height;

				depthAttachmentInfo.imageView = depthStencil.RT->get()->requireView(
					graphics::helper::buildBasicImageSubresource(getImageAspectFlags(depthStencil.depthStencilOp)), VK_IMAGE_VIEW_TYPE_2D, false, false).handle.get();
				depthAttachmentInfo.clearValue = depthStencil.clearValue;
				depthAttachmentInfo.loadOp = getAttachmentLoadOp(depthStencil.Op);
				depthAttachmentInfo.storeOp = getAttachmentStoreOp(depthStencil.Op);
			}

			std::vector<VkRenderingAttachmentInfo> attachments { };
			uint32 RTCount = 0;
			for (auto& RT : RTs)
			{
				if (RT.RT == nullptr) { break; }

				RTCount++;
				if (width == ~0 || height == ~0)
				{
					width = RT.RT->get()->getExtent().width;
					height = RT.RT->get()->getExtent().height;
				}

				attachments.emplace_back(helper::renderingAttachmentInfo(false));
				attachments.back().imageView = RT.RT->get()->requireView(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D, false, false).handle.get();
				attachments.back().clearValue = RT.clearValue;
				attachments.back().loadOp = getAttachmentLoadOp(RT.Op);
				attachments.back().storeOp = getAttachmentStoreOp(RT.Op);
			}

			auto renderInfo = helper::renderingInfo();
			{
				renderInfo.renderArea = VkRect2D{ .offset {0,0}, .extent = {.width = width, .height = height } };
				renderInfo.colorAttachmentCount = attachments.size();
				renderInfo.pColorAttachments = attachments.data();

				if (bDepthStencilValue)
				{
					renderInfo.pDepthAttachment = &depthAttachmentInfo;
				}
			}

			auto& cmd = queue.getActiveCmd()->commandBuffer;
			vkCmdBeginRendering(cmd, &renderInfo);

			helper::dynamicStateGeneralSet(cmd);

			if (RTCount > 0)
			{
				vkCmdSetColorBlendEnableEXT(cmd, 0, RTCount, bEnableBlends);
				vkCmdSetColorBlendEquationEXT(cmd, 0, RTCount, colorBlendEquations);
			}

			helper::setScissor(cmd, { 0, 0 }, { width, height });
			helper::setViewport(cmd, width, height);

			// Depth test cast.
			vkCmdSetDepthWriteEnable(cmd, hasFlag(depthStencil.depthStencilOp, EDepthStencilOp::DepthWrite) ? VK_TRUE : VK_FALSE);
		}

		void endRendering(graphics::GraphicsQueue& queue) const
		{
			using namespace graphics;
			queue.checkRecording();

			vkCmdEndRendering(queue.getActiveCmd()->commandBuffer);
		}

	private:
		void transitionForRender(graphics::GraphicsQueue& queue) const
		{
			queue.checkRecording();

			for (auto& RT : RTs)
			{
				if (RT.RT == nullptr)
				{
					break;
				}
				queue.transitionColorAttachment(RT.RT->get());
			}

			if (depthStencil.RT)
			{
				queue.transitionDepthStencilAttachment(depthStencil.RT->get(), depthStencil.depthStencilOp);
			}
		}
	};

	inline static uint32 asSRV(
		graphics::GraphicsOrComputeQueue& queue, 
		graphics::GPUTextureRef texture,
		const VkImageSubresourceRange& range = graphics::helper::buildBasicImageSubresource(),
		VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D)
	{
		queue.transitionSRV(texture, range);
		return texture->requireView(range, viewType, true, false).SRV.get();
	}
}