#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/lighting.h>
#include <shader/lighting.hlsl>
#include <shader/pcss.hlsl>
#include <scene/system/shadow.h>
#include <shader/blur3x3.hlsl>

namespace chord
{
	using namespace graphics;

	class LightingCS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);
		class SV_ShadingType : SHADER_VARIANT_ENUM("LIGHTING_TYPE", EShadingType);
		using Permutation = TShaderVariantVector<SV_ShadingType>;
	};
	IMPLEMENT_GLOBAL_SHADER(LightingCS, "resource/shader/lighting.hlsl", "mainCS", EShaderStage::Compute);

	PRIVATE_GLOBAL_SHADER(ShadowProjectionMaskBlur_CS, "resource/shader/blur3x3.hlsl", "blurShadowMask", EShaderStage::Compute);
	PRIVATE_GLOBAL_SHADER(ShadowProjectionPCSS_CS, "resource/shader/pcss.hlsl", "percentageCloserSoftShadowCS", EShaderStage::Compute);

	CascadeShadowEvaluateResult chord::cascadeShadowEvaluate(
		graphics::GraphicsQueue& queue, 
		GBufferTextures& gbuffers, 
		uint32 cameraViewId, 
		const CascadeShadowContext& cascadeCtx, 
		graphics::PoolTextureRef softShadowMaskHistory)
	{
		CascadeShadowEvaluateResult result{ };


		if (!cascadeCtx.isValid())
		{
			return result;
		}

		if (cascadeCtx.bDirectionChange)
		{
			softShadowMaskHistory = nullptr;
		}

		result.softShadowMask = getContext().getTexturePool().create(
			"softShadowMask",
			(gbuffers.depthStencil->get().getExtent().width  + 7) / 8,
			(gbuffers.depthStencil->get().getExtent().height + 7) / 8,
			VK_FORMAT_R8_UNORM,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		ShadowProjectionPushConsts pushConst{};
		pushConst.cameraViewId = cameraViewId;
		pushConst.cascadeCount = cascadeCtx.config.cascadeCount;
		pushConst.shadowViewId = cascadeCtx.viewsSRV;
		pushConst.depthId = asSRV(queue, gbuffers.depthStencil, helper::buildDepthImageSubresource());

		pushConst.lightDirection = math::normalize(cascadeCtx.direction);
		pushConst.normalId = asSRV(queue, gbuffers.gbufferA);
		pushConst.shadowMapTexelSize = 1.0f / float(cascadeCtx.config.cascadeDim);
		pushConst.normalOffsetScale = cascadeCtx.config.normalOffsetScale;

		pushConst.lightSize = cascadeCtx.config.lightSize;
		pushConst.cascadeBorderJitterCount = cascadeCtx.config.cascadeBorderJitterCount;
		pushConst.pcfBiasScale = cascadeCtx.config.pcfBiasScale;

		pushConst.biasLerpMin_const = cascadeCtx.config.biasLerpMin_const;
		pushConst.biasLerpMax_const = cascadeCtx.config.biasLerpMax_const;

		pushConst.bContactHardenPCF = cascadeCtx.config.bContactHardenPCF;
		pushConst.shadowDepthIds = cascadeCtx.cascadeShadowDepthIds;
		pushConst.realtimeCascadeCount = cascadeCtx.config.realtimeCascadeCount;
		pushConst.blockerMinSampleCount = cascadeCtx.config.minBlockerSearchSampleCount;
		pushConst.blockerMaxSampleCount = cascadeCtx.config.maxBlockerSearchSampleCount;
		pushConst.pcfMinSampleCount = cascadeCtx.config.minPCFSampleCount;
		pushConst.pcfMaxSampleCount = cascadeCtx.config.maxPCFSampleCount;
		pushConst.blockerSearchMaxRangeScale = cascadeCtx.config.blockerSearchMaxRangeScale;

		auto filteredOut = softShadowMaskHistory;
		uint passCount = cascadeCtx.config.shadowMaskFilterCount;
		if (softShadowMaskHistory && passCount > 0)
		{
			auto filteredIn = softShadowMaskHistory;
			filteredOut = getContext().getTexturePool().create(
				"softShadowMask-2",
				(gbuffers.depthStencil->get().getExtent().width  + 7) / 8,
				(gbuffers.depthStencil->get().getExtent().height + 7) / 8,
				VK_FORMAT_R8_UNORM,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

			for (uint i = 0; i < passCount; i++)
			{
				uint2 dim = { softShadowMaskHistory->get().getExtent().width,  softShadowMaskHistory->get().getExtent().height };

				const uint2 dispatchDim = divideRoundingUp(dim, uint2(8));
				Blur3x3PushConsts push { };

				push.dim = dim;
				push.srv = asSRV(queue, filteredIn);
				push.uav = asUAV(queue, filteredOut);

				auto computeShader = getContext().getShaderLibrary().getShader<ShadowProjectionMaskBlur_CS>();
				addComputePass2(queue, "ShadowProjection One Tap Mask Blur", getContext().computePipe(computeShader, "ShadowProjectionOneTapMaskBlur_CS-Pipe"), push, { dispatchDim.x, dispatchDim.y, 1 });

				if (i < passCount - 1)
				{
					std::swap(filteredIn, filteredOut);
				}
			}
		}

		{
			uint2 dim = { gbuffers.color->get().getExtent().width,  gbuffers.color->get().getExtent().height };

			const uint2 dispatchDim = divideRoundingUp(dim, uint2(8));
			auto copyPush = pushConst;
			copyPush.softShadowMaskTexture = filteredOut != nullptr ? asSRV(queue, filteredOut) : kUnvalidIdUint32;

			copyPush.uav  = asUAV(queue, gbuffers.color);
			copyPush.uav1 = asUAV(queue, result.softShadowMask);

			auto computeShader = getContext().getShaderLibrary().getShader<ShadowProjectionPCSS_CS>();
			addComputePass2(queue, "ShadowProjection PCSS CS", getContext().computePipe(computeShader, "ShadowProjection-PCSS-Pipe"), copyPush, { dispatchDim.x, dispatchDim.y, 1 });
		}

		return result;
	}

	void chord::lighting(
		GraphicsQueue& queue, 
		GBufferTextures& gbuffers, 
		uint32 cameraViewId, 
		PoolBufferGPUOnlyRef drawMeshletCmdBuffer, 
		const VisibilityTileMarkerContext& marker)
	{

		uint sceneColorUAV = asUAV(queue, gbuffers.color);
		uint normalUAV     = asUAV(queue, gbuffers.gbufferA);

		for (uint i = 0; i < uint(EShadingType::MAX); i++)
		{
			if (i == kLightingType_None) { continue; }

			auto lightingTileCtx = prepareShadingTileParam(queue, EShadingType(i), marker);

			LightingPushConsts pushConst{};
			pushConst.visibilityTexelSize = math::vec2(1.0f) / math::vec2(marker.visibilityDim);
			pushConst.visibilityDim = marker.visibilityDim;
			pushConst.cameraViewId = cameraViewId;
			pushConst.tileBufferCmdId = asSRV(queue, lightingTileCtx.tileCmdBuffer);
			pushConst.visibilityId = asSRV(queue, marker.visibilityTexture);
			pushConst.sceneColorId = sceneColorUAV;
			pushConst.normalRSId = normalUAV;
			pushConst.drawedMeshletCmdId = asSRV(queue, drawMeshletCmdBuffer);

			LightingCS::Permutation CSPermutation;
			CSPermutation.set<LightingCS::SV_ShadingType>(EShadingType(i));
			auto computeShader = getContext().getShaderLibrary().getShader<LightingCS>(CSPermutation);
			addIndirectComputePass2(
				queue,
				"LightingTile CS",
				getContext().computePipe(computeShader, "LightingTilePipe"),
				pushConst,
				lightingTileCtx.dispatchIndirectBuffer);
		}
	}
}