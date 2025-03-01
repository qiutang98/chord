#include <shader_compiler/shader.h>
#include <shader_compiler/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/lighting.h>
#include <shader/lighting.hlsl>
#include <shader/pcss.hlsl>
#include <scene/system/shadow.h>
#include <shader/blur3x3.hlsl>
#include <shader/half_downsample.hlsl>
#include <shader/disocclusion_mask.hlsl>

namespace chord
{
	using namespace graphics;

	static uint32 sDrawFullScreenSkyBatchSize = 2;
	static AutoCVarRef cVarDrawFullScreenSkyBatchSize(
		"r.sky.drawFullScreenBatchSize",
		sDrawFullScreenSkyBatchSize,
		"Draw full screen sky batch size.");

	class DrawSkyCS : public GlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(GlobalShader);

		class SV_BatchSize : SHADER_VARIANT_RANGE_INT("DRAW_SKY_BATCH_SIZE", 1, 2);
		using Permutation = TShaderVariantVector<SV_BatchSize>;
	};
	IMPLEMENT_GLOBAL_SHADER(DrawSkyCS, "resource/shader/lighting.hlsl", "drawSkyCS", EShaderStage::Compute);

	class LightingCS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);
		class SV_ShadingType : SHADER_VARIANT_ENUM("LIGHTING_TYPE", EShadingType);
		using Permutation = TShaderVariantVector<SV_ShadingType>;
	};
	IMPLEMENT_GLOBAL_SHADER(LightingCS, "resource/shader/lighting.hlsl", "mainCS", EShaderStage::Compute);

	PRIVATE_GLOBAL_SHADER(HalfGbufferDownsample_CS, "resource/shader/half_downsample.hlsl", "mainCS", EShaderStage::Compute);
	PRIVATE_GLOBAL_SHADER(DisocclusionMask_CS, "resource/shader/disocclusion_mask.hlsl", "mainCS", EShaderStage::Compute);

	PRIVATE_GLOBAL_SHADER(ShadowProjectionMaskBlur_CS, "resource/shader/blur3x3.hlsl", "blurShadowMask", EShaderStage::Compute);
	PRIVATE_GLOBAL_SHADER(ShadowProjectionPCSS_CS, "resource/shader/pcss.hlsl", "percentageCloserSoftShadowCS", EShaderStage::Compute);

	CascadeShadowEvaluateResult chord::cascadeShadowEvaluate(
		graphics::GraphicsQueue& queue, 
		GBufferTextures& gbuffers, 
		uint32 cameraViewId, 
		const CascadeShadowContext& cascadeCtx, 
		graphics::PoolTextureRef softShadowMaskHistory,
		graphics::PoolTextureRef disocclusionMask)
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
		pushConst.disocclusionMaskId = disocclusionMask != nullptr ? asSRV(queue, disocclusionMask) : kUnvalidIdUint32;
		pushConst.lightDirection = math::normalize(cascadeCtx.direction);
		pushConst.normalId = asSRV(queue, gbuffers.vertexRSNormal);
		pushConst.shadowMapTexelSize = 1.0f / float(cascadeCtx.config.cascadeDim);
		pushConst.normalOffsetScale = cascadeCtx.config.normalOffsetScale;
		pushConst.motionVectorId = asSRV(queue, gbuffers.motionVector);
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

	void chord::drawFullScreenSky(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers, 
		uint32 cameraViewId, 
		const AtmosphereLut& skyLuts)
	{
		LightingPushConsts pushConst{};
		pushConst.visibilityTexelSize = math::vec2(1.0f) / math::vec2(gbuffers.dimension);
		pushConst.visibilityDim = gbuffers.dimension;
		pushConst.cameraViewId  = cameraViewId;
		pushConst.sceneColorId  = asUAV(queue, gbuffers.color);

		pushConst.linearSampler       = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
		pushConst.irradianceTextureId = asSRV(queue, skyLuts.irradianceTexture);
		pushConst.transmittanceId     = asSRV(queue, skyLuts.transmittance);
		pushConst.scatteringId        = asSRV3DTexture(queue, skyLuts.scatteringTexture);
		if (skyLuts.optionalSingleMieScatteringTexture != nullptr)
		{
			pushConst.singleMieScatteringId = asSRV3DTexture(queue, skyLuts.optionalSingleMieScatteringTexture);
		}

		const uint32 kBatchSize = math::clamp(sDrawFullScreenSkyBatchSize, 1U, 2U);
		const uint2 dispatchSize = divideRoundingUp(gbuffers.dimension, uint2(8 * kBatchSize));

		DrawSkyCS::Permutation CSPermutation;
		CSPermutation.set<DrawSkyCS::SV_BatchSize>(kBatchSize);
		auto computeShader = getContext().getShaderLibrary().getShader<DrawSkyCS>(CSPermutation);
		addComputePass2(
			queue,
			"DrawFullScreenSky CS",
			getContext().computePipe(computeShader, "DrawFullScreenSkyPipe"),
			pushConst,
			{ dispatchSize, 1U });
	}

	void chord::lighting(
		GraphicsQueue& queue, 
		GBufferTextures& gbuffers, 
		uint32 cameraViewId, 
		PoolBufferGPUOnlyRef drawMeshletCmdBuffer, 
		const AtmosphereLut& skyLuts,
		const VisibilityTileMarkerContext& marker)
	{
		uint sceneColorUAV          = asUAV(queue, gbuffers.color);
		uint vertexNormalUAV        = asUAV(queue, gbuffers.vertexRSNormal);
		uint pixelNormalUAV         = asUAV(queue, gbuffers.pixelRSNormal);
		uint motionVectorUAV        = asUAV(queue, gbuffers.motionVector);
		uint aoRoughnessMetallicUAV = asUAV(queue, gbuffers.aoRoughnessMetallic);
		uint baseColorUAV           = asUAV(queue, gbuffers.baseColor);
		for (uint i = 0; i < uint(EShadingType::MAX); i++)
		{
			auto lightingTileCtx = prepareShadingTileParam(queue, EShadingType(i), marker);

			LightingPushConsts pushConst{};
			pushConst.visibilityTexelSize = math::vec2(1.0f) / math::vec2(marker.visibilityDim);
			pushConst.visibilityDim = marker.visibilityDim;
			pushConst.cameraViewId = cameraViewId;
			pushConst.tileBufferCmdId = asSRV(queue, lightingTileCtx.tileCmdBuffer);
			pushConst.visibilityId = asSRV(queue, marker.visibilityTexture);
			pushConst.sceneColorId = sceneColorUAV;
			pushConst.vertexNormalRSId = vertexNormalUAV;
			pushConst.pixelNormalRSId = pixelNormalUAV;
			pushConst.motionVectorId = motionVectorUAV;
			pushConst.aoRoughnessMetallicId = aoRoughnessMetallicUAV;
			pushConst.baseColorId = baseColorUAV;
			pushConst.drawedMeshletCmdId = asSRV(queue, drawMeshletCmdBuffer);
			pushConst.linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();

			pushConst.irradianceTextureId = asSRV(queue, skyLuts.irradianceTexture);
			pushConst.transmittanceId     = asSRV(queue, skyLuts.transmittance);
			pushConst.scatteringId        = asSRV3DTexture(queue, skyLuts.scatteringTexture);
			if (skyLuts.optionalSingleMieScatteringTexture != nullptr)
			{
				pushConst.singleMieScatteringId = asSRV3DTexture(queue, skyLuts.optionalSingleMieScatteringTexture);
			}

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

	void chord::computeHalfResolutionGBuffer(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers)
	{
		// Now generate half resolution buffer.
		gbuffers.generateHalfGbuffer();
		{
			HalfDownsamplePushConsts pushConsts{};

			const uint2 dispatchDim = divideRoundingUp(gbuffers.dimension / 2U, uint2(8));

			pushConsts.depthTextureId               = asSRV(queue, gbuffers.depthStencil, helper::buildDepthImageSubresource());
			pushConsts.pixelNormalTextureId         = asSRV(queue, gbuffers.pixelRSNormal);
			pushConsts.aoRoughnessMetallicTextureId = asSRV(queue, gbuffers.aoRoughnessMetallic);
			pushConsts.motionVectorTextureId        = asSRV(queue, gbuffers.motionVector);
			pushConsts.vertexNormalTextureId        = asSRV(queue, gbuffers.vertexRSNormal);


			pushConsts.halfPixelNormalRSId = asUAV(queue, gbuffers.pixelRSNormal_Half);
			pushConsts.halfDeviceZId       = asUAV(queue, gbuffers.depth_Half);
			pushConsts.halfMotionVectorId  = asUAV(queue, gbuffers.motionVector_Half);
			pushConsts.halfRoughnessId     = asUAV(queue, gbuffers.roughness_Half);
			pushConsts.halfVertexNormalId  = asUAV(queue, gbuffers.vertexRSNormal_Half);

			pushConsts.workDim                  = gbuffers.dimension / 2U;
			pushConsts.workTexelSize            = 1.0f / float2(pushConsts.workDim);
			pushConsts.pointClampedEdgeSampler  = getContext().getSamplerManager().pointClampEdge().index.get();
			pushConsts.linearClampedEdgeSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
			pushConsts.srcTexelSize             = 1.0f / float2(gbuffers.dimension);

			auto computeShader = getContext().getShaderLibrary().getShader<HalfGbufferDownsample_CS>();
			addComputePass2(queue, "GBufferHalfDownSample_CS", getContext().computePipe(computeShader, "GBufferHalfDownSample_Pipe"), pushConsts, { dispatchDim.x, dispatchDim.y, 1 });
		}
	}

	DisocclusionPassResult chord::computeDisocclusionMask(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::PoolTextureRef depth_Half_LastFrame,
		graphics::PoolTextureRef vertexNormalRS_Half_LastFrame)
	{
		uint halfWidth = gbuffers.dimension.x / 2;
		uint halfHeight = gbuffers.dimension.y / 2;

		const uint2 dispatchDim = divideRoundingUp(gbuffers.dimension / 2U, uint2(8));
		DisocclusionMaskPushConsts pushConsts{};

		pushConsts.workDim = gbuffers.dimension / 2U;
		pushConsts.workTexelSize = 1.0f / float2(pushConsts.workDim);

		pushConsts.cameraViewId = cameraViewId;
		pushConsts.motionVectorId = asSRV(queue, gbuffers.motionVector_Half);
		pushConsts.depthTextureId = asSRV(queue, gbuffers.depth_Half);
		pushConsts.depthTextureId_LastFrame = asSRV(queue, depth_Half_LastFrame);
		pushConsts.normalRSId = asSRV(queue, gbuffers.vertexRSNormal_Half);
		pushConsts.normalRSId_LastFrame = asSRV(queue, vertexNormalRS_Half_LastFrame);
		pushConsts.pointClampedEdgeSampler = getContext().getSamplerManager().pointClampEdge().index.get();
		pushConsts.linearClampedEdgeSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();

		auto mask = getContext().getTexturePool().create(
			"DisocclusionMask_Half", 
			halfWidth, 
			halfHeight, 
			VK_FORMAT_R8_UNORM, 
			VK_IMAGE_USAGE_SAMPLED_BIT| VK_IMAGE_USAGE_STORAGE_BIT);

		pushConsts.disocclusionMaskUAV = asUAV(queue, mask);

		auto computeShader = getContext().getShaderLibrary().getShader<DisocclusionMask_CS>();
		addComputePass2(queue, "DisocclusionMask_CS", getContext().computePipe(computeShader, "DisocclusionMask_Pipe"), pushConsts, { dispatchDim.x, dispatchDim.y, 1 });
	
		DisocclusionPassResult result{};
		result.disocclusionMask = mask;

		return result;
	}


}