#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_helper.h>
#include <renderer/visibility_tile.h>
#include <renderer/render_textures.h>
#include <renderer/mesh/gltf_rendering.h>
#include <scene/manager/manager_ddgi.h>

namespace chord
{
	struct ShadowConfig;
	struct CascadeInfo;

	struct CascadeShadowEvaluateResult
	{
		graphics::PoolTextureRef softShadowMask = nullptr; // 1/8 res
	};

	extern CascadeShadowEvaluateResult cascadeShadowEvaluate(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		const CascadeShadowContext& cascadeCtx,
		graphics::PoolTextureRef softShadowMask,
		graphics::PoolTextureRef disocclusionMask);

	extern void drawFullScreenSky(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers,
		uint32 cameraViewId, 
		const AtmosphereLut& skyLuts);

	extern void lighting(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::PoolBufferGPUOnlyRef drawMeshletCmdBuffer,
		const AtmosphereLut& skyLuts,
		const VisibilityTileMarkerContext& marker);

	extern void computeHalfResolutionGBuffer(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers);

	struct DisocclusionPassResult
	{
		graphics::PoolTextureRef disocclusionMask;
	};
	extern DisocclusionPassResult computeDisocclusionMask(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::PoolTextureRef depth_Half_LastFrame,
		graphics::PoolTextureRef vertexNormalRS_Half_LastFrame);

	extern void visualizeNanite(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::PoolBufferGPUOnlyRef drawMeshletCmdBuffer,
		const VisibilityTileMarkerContext& marker);

	extern void visualizeAccelerateStructure(
		graphics::GraphicsQueue& queue,
		const AtmosphereLut& luts,
		const CascadeShadowContext& cascadeCtx,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::helper::AccelKHRRef tlas);

	extern graphics::PoolTextureRef computeBRDFLut(
		graphics::GraphicsOrComputeQueue& queue,
		uint lutDim);

	using RendererTimerLambda = std::function<void(const std::string&, graphics::GraphicsOrComputeQueue& queue)>;
	extern void giUpdate(
		graphics::CommandList& cmd,
		graphics::GraphicsQueue& queue,
		const AtmosphereLut& luts,
		const CascadeShadowContext& cascadeCtx,
		GIContext& giWorldProbeCtx,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::helper::AccelKHRRef tlas,
		const DisocclusionPassResult& disocclusionCtx,
		ICamera* camera,
		graphics::PoolTextureRef hzb,
		const PerframeCameraView& perframe,
		graphics::PoolTextureRef depth_Half_LastFrame,
		graphics::PoolTextureRef pixelNormalRS_Half_LastFrame, 
		bool bCameraCut,
		RendererTimerLambda timer);

	extern void ddgiUpdate(
		graphics::CommandList& cmd,
		graphics::GraphicsQueue& queue,
		const AtmosphereLut& luts,
		const DDGIConfigCPU& config,
		const CascadeShadowContext& cascadeCtx,
		GBufferTextures& gbuffers,
		DDGIContext& ddgiCtx,
		uint32 cameraViewId,
		graphics::helper::AccelKHRRef tlas,
		ICamera* camera,
		graphics::PoolTextureRef hzb);
}