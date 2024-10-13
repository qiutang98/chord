#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/bufferpool.h>
#include <graphics/common.h>

namespace chord
{
	struct DepthStencilRT
	{
		DepthStencilRT() = default;

		DepthStencilRT(graphics::PoolTextureRef inRT, graphics::EDepthStencilOp inDepthOp, graphics::ERenderTargetLoadStoreOp inLoadStoreOp, VkClearValue inClearValue = {})
			: RT(inRT)
			, depthStencilOp(inDepthOp)
			, Op(inLoadStoreOp)
			, clearValue(inClearValue)
		{

		}

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

	enum class EBlendMode
	{
		Translucency,
		Additive,

		MAX,
	};
	inline VkColorBlendEquationEXT getBlendMode(EBlendMode blendMode)
	{
		VkColorBlendEquationEXT result{};

		// Src:  shader compute color result.
		// Dest: Render target already exist color.
		switch (blendMode)
		{
		case chord::EBlendMode::Translucency:
		{
			// color.rgb = src.rgb * src.a + dest.rgb * (1 - src.a);
			// color.a = src.a;
			result.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			result.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			result.colorBlendOp = VK_BLEND_OP_ADD;
			result.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			result.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			result.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		break;
		case chord::EBlendMode::Additive:
		{
			// color.rgb = src.rgb * src.a + dest.rgb;
			// color.a = src.a;
			result.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			result.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			result.colorBlendOp = VK_BLEND_OP_ADD;
			result.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			result.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			result.alphaBlendOp = VK_BLEND_OP_ADD;
		}
		break;
		default:
			checkEntry();
			break;
		}
		return result;
	}

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

			for (auto& mask : colorComponentMasks)
			{
				mask =
					VK_COLOR_COMPONENT_R_BIT |
					VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT |
					VK_COLOR_COMPONENT_A_BIT;
			}
		}

		RenderTargetRT RTs[graphics::kMaxRenderTargets];
		VkBool32 bEnableBlends[graphics::kMaxRenderTargets];
		VkColorBlendEquationEXT colorBlendEquations[graphics::kMaxRenderTargets];
		VkColorComponentFlags colorComponentMasks[graphics::kMaxRenderTargets];
		DepthStencilRT depthStencil;

		std::vector<VkFormat> getRTsFormats() const
		{
			std::vector<VkFormat> result { };
			for (auto& RT : RTs)
			{
				if(RT.RT == nullptr) { break; }
				result.push_back(RT.RT->get().getFormat());
			}
			return std::move(result);
		}

		VkFormat getDepthFormat() const
		{
			if (depthStencil.RT == nullptr) return VK_FORMAT_UNDEFINED;
			return depthStencil.RT->get().getFormat();
		}

		VkFormat getStencilFormat() const
		{
			if (depthStencil.RT == nullptr) return VK_FORMAT_UNDEFINED;
			if (!graphics::isFormatExistStencilComponent(depthStencil.RT->get().getFormat()))
			{
				return VK_FORMAT_UNDEFINED;
			}

			return depthStencil.RT->get().getFormat();
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
				width = depthStencil.RT->get().getExtent().width;
				height = depthStencil.RT->get().getExtent().height;

				
				VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
				if (graphics::isFormatExistStencilComponent(depthStencil.RT->get().getFormat()))
				{
					flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}

				depthAttachmentInfo.imageView = depthStencil.RT->get().requireView(
					graphics::helper::buildBasicImageSubresource(flags), VK_IMAGE_VIEW_TYPE_2D, false, false).handle.get();
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
					width = RT.RT->get().getExtent().width;
					height = RT.RT->get().getExtent().height;
				}

				attachments.emplace_back(helper::renderingAttachmentInfo(false));
				attachments.back().imageView = RT.RT->get().requireView(helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_2D, false, false).handle.get();
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

				vkCmdSetColorWriteMaskEXT(cmd, 0, RTCount, colorComponentMasks);
			}
			else
			{
				static const VkBool32 bEnable = VK_FALSE;
				vkCmdSetColorBlendEnableEXT(cmd, 0, 1, &bEnable);

				static const VkColorBlendEquationEXT blendEquation = {};
				vkCmdSetColorBlendEquationEXT(cmd, 0, 1, &blendEquation);

				static const VkColorComponentFlags fullMask =
					VK_COLOR_COMPONENT_R_BIT |
					VK_COLOR_COMPONENT_G_BIT |
					VK_COLOR_COMPONENT_B_BIT |
					VK_COLOR_COMPONENT_A_BIT;
				vkCmdSetColorWriteMaskEXT(cmd, 0, 1, &fullMask);
			}

			helper::setScissor(cmd, { 0, 0 }, { width, height });
			helper::setViewport(cmd, width, height, true);

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
				queue.transitionColorAttachment(RT.RT);
			}

			if (depthStencil.RT)
			{
				queue.transitionDepthStencilAttachment(depthStencil.RT, depthStencil.depthStencilOp);
			}
		}
	};

	inline bool isDepthFormat(graphics::PoolTextureRef texture)
	{
		const auto format = texture->get().getFormat();

		return format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
	}

	inline static uint32 asSRV(
		graphics::GraphicsOrComputeQueue& queue, 
		graphics::PoolTextureRef texture,
		const VkImageSubresourceRange& range = graphics::helper::buildBasicImageSubresource(),
		VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D)
	{
		auto rangeTransition = range;

		if (hasFlag(range.aspectMask, VK_IMAGE_ASPECT_DEPTH_BIT) && 
			graphics::isFormatExistStencilComponent(texture->get().getFormat()))
		{
			rangeTransition.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		queue.transitionSRV(texture, rangeTransition);
		return texture->get().requireView(range, viewType, true, false).SRV.get();
	}

	inline static uint32 asSRV3DTexture(
		graphics::GraphicsOrComputeQueue& queue,
		graphics::PoolTextureRef texture,
		const VkImageSubresourceRange& range = graphics::helper::buildBasicImageSubresource())
	{
		queue.transitionSRV(texture, range);
		return texture->get().requireView(range, VK_IMAGE_VIEW_TYPE_3D, true, false).SRV.get();
	}

	inline static uint32 asUAV(
		graphics::GraphicsOrComputeQueue& queue,
		graphics::PoolTextureRef texture,
		const VkImageSubresourceRange& range = graphics::helper::buildBasicImageSubresource(),
		VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D)
	{
		check(hasFlag(texture->get().getInfo().usage, VK_IMAGE_USAGE_STORAGE_BIT));

		queue.transitionUAV(texture, range);
		return texture->get().requireView(range, viewType, false, true).UAV.get();
	}

	inline static uint32 asUAV3DTexture(
		graphics::GraphicsOrComputeQueue& queue,
		graphics::PoolTextureRef texture,
		const VkImageSubresourceRange& range = graphics::helper::buildBasicImageSubresource())
	{
		check(hasFlag(texture->get().getInfo().usage, VK_IMAGE_USAGE_STORAGE_BIT));

		queue.transitionUAV(texture, range);
		return texture->get().requireView(range, VK_IMAGE_VIEW_TYPE_3D, false, true).UAV.get();
	}


	inline static uint32 asUAV(
		graphics::GraphicsOrComputeQueue& queue,
		graphics::PoolBufferRef buffer
	)
	{
		check(hasFlag(buffer->get().getUsage(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
		queue.transitionUAV(buffer);
		return buffer->get().requireView(true, false).storage.get();
	}

	inline static uint32 asSRV(
		graphics::GraphicsOrComputeQueue& queue,
		graphics::PoolBufferRef buffer
	)
	{
		check(hasFlag(buffer->get().getUsage(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
		queue.transitionSRV(buffer);
		return buffer->get().requireView(true, false).storage.get();
	}

	class PushSetBuilder
	{
	public:
		PushSetBuilder(graphics::GraphicsOrComputeQueue& queue, VkCommandBuffer cmd) :
			m_queue(queue),
			m_cmd(cmd) { }

		PushSetBuilder& addSRV(graphics::PoolBufferRef buffer);
		PushSetBuilder& addUAV(graphics::PoolBufferRef buffer);

		PushSetBuilder& addSRV(
			graphics::PoolTextureRef image,
			const VkImageSubresourceRange& range = graphics::helper::buildBasicImageSubresource(),
			VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);

		PushSetBuilder& addUAV(
			graphics::PoolTextureRef image,
			const VkImageSubresourceRange& range = graphics::helper::buildBasicImageSubresource(),
			VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);

		void push(graphics::ComputePipelineRef pipe, uint32 set);

	private:
		struct CacheBindingBuilder
		{
			enum class EType
			{
				BufferSRV,
				BufferUAV,
				TextureSRV,
				TextureUAV,
			} type;

			graphics::PoolTextureRef image;
			VkImageSubresourceRange imageRange;
			VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;

			VkBuffer buffer;
			VkDescriptorType descriptorType;
			VkDescriptorBufferInfo bufferInfo;
		};
		graphics::GraphicsOrComputeQueue& m_queue;
		VkCommandBuffer m_cmd;
		std::vector<CacheBindingBuilder> m_cacheBindingBuilder;
	};

	struct HZBContext
	{
		graphics::PoolTextureRef minHZB = nullptr;
		graphics::PoolTextureRef maxHZB = nullptr;

		// Valid range of depth, excluded sky.
		graphics::PoolBufferRef validDepthRange = nullptr;

		// Texture extent.
		math::uvec2 dimension; 
		uint32 mipmapLevelCount;

		// 
		math::uvec2 mip0ValidArea;
		math::vec2  validUv;

		HZBContext() {};
		HZBContext(
			graphics::PoolTextureRef minHzb, 
			graphics::PoolTextureRef maxHzb,
			graphics::PoolBufferRef validDepthRange,
			math::uvec2 dimension,
			uint32 mipmapLevelCount,
			math::uvec2 mip0ValidArea,
			math::vec2  validUv)
			: minHZB(minHzb)
			, maxHZB(maxHzb)
			, validDepthRange(validDepthRange)
			, dimension(dimension)
			, mipmapLevelCount(mipmapLevelCount)
			, mip0ValidArea(mip0ValidArea)
			, validUv(validUv)
		{

		}

		bool isValid() const
		{
			return minHZB != nullptr;
		}
	};



	struct CascadeInfo
	{
		float4x4 viewProjectMatrix;
		float4 orthoDepthConvertToView; 
		float4 frustumPlanes[6];
	};


	struct CascadeShadowMapConfig
	{
		CHORD_DEFAULT_COMPARE_ARCHIVE(CascadeShadowMapConfig);

		int32  cascadeCount         = 8;
		int32  realtimeCascadeCount = 3;
		uint32 cascadeDim           = 2048;

		// 
		float cascadeStartDistance = 0.0f;
		float cascadeEndDistance    = 80.0f;
		float farCascadeEndDistance = 800.0f;
		float splitLambda           = 0.8f;
		float farCascadeSplitLambda = 0.8f;


		// 
		float normalOffsetScale    = 0.0f;
		float shadowBiasConst      = 0.0f;
		float shadowBiasSlope      = 0.0f;
		float radiusScaleFixed     = 1.0f;

		//
		float lightSize                = 1.0f;
		float cascadeBorderJitterCount = 2.0f;
		float pcfBiasScale             = 20.0f;

		float biasLerpMin_const = 5.0f;
		float biasLerpMax_const = 10.0f;
		bool  bContactHardenPCF = false;

		// 
		int shadowMaskFilterCount = 0;

		int minPCFSampleCount = 4;
		int maxPCFSampleCount = 32;

		int minBlockerSearchSampleCount = 2;
		int maxBlockerSearchSampleCount = 16;
		float blockerSearchMaxRangeScale = 0.125f;
	};

	struct ShadowConfig
	{
		CHORD_DEFAULT_COMPARE_ARCHIVE(ShadowConfig);

		CascadeShadowMapConfig cascadeConfig;
	};

	struct CascadeShadowContext
	{
		// Cache render config.
		CascadeShadowMapConfig config;
		uint32 cascadeShadowDepthIds;

		//
		bool bDirectionChange = true;

		float3 direction = { 0.0f, 0.0f, 0.0f };

		// Cascade views.
		graphics::PoolBufferGPUOnlyRef views = nullptr;
		uint32 viewsSRV = ~0;

		// Shadow depths.
		std::vector<graphics::PoolTextureRef> depths;

		CascadeShadowContext() {};
		CascadeShadowContext(const CascadeShadowMapConfig& config)
			: config(config)
		{

		}

		bool isValid() const
		{
			return views != nullptr;
		}
	};

	struct CascadeShadowHistory
	{
		float3 direction = { 0.0f, 0.0f, 0.0f }; 

		// Cache render config.
		CascadeShadowMapConfig config;

		// Cascade views.
		graphics::PoolBufferGPUOnlyRef views = nullptr;
		uint32 viewsSRV = ~0U;

		// Cache render depths.
		std::vector<graphics::PoolTextureRef> depths;
		graphics::PoolTextureRef softShadowMask = nullptr;
	};

	struct DeferredRendererHistory
	{
		HZBContext hzbCtx;
		CascadeShadowHistory cascadeCtx;
	};
}