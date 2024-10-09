#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_textures.h>
#include <scene/scene_common.h>
#include <renderer/postprocessing/postprocessing.h>
#include <utils/camera.h>
#include <renderer/render_helper.h>

namespace chord
{
	struct GLTFRenderContext
	{
		const PerframeCollected* perframeCollect;
		const uint gltfObjectCount;
		const uint gltfBufferId;
		const uint cameraView;
		std::function<void(const std::string&, graphics::GraphicsOrComputeQueue& queue)> timerLambda = nullptr;

		GLTFRenderContext(
			const PerframeCollected* inPerframeCollected,
			const uint inGLTFObjectCount,
			const uint inGLTFBufferId,
			const uint inCameraViewId)
			: perframeCollect(inPerframeCollected)
			, gltfObjectCount(inGLTFObjectCount)
			, gltfBufferId(inGLTFBufferId)
			, cameraView(inCameraViewId)
		{

		}
	};

	extern bool shouldRenderGLTF(const GLTFRenderContext& renderCtx);
	extern bool enableGLTFHZBCulling();
	extern CountAndCmdBuffer instanceCulling(
		graphics::GraphicsQueue& queue, 
		const GLTFRenderContext& ctx, 
		uint instanceCullingViewInfo, 
		uint instanceCullingViewInfoOffset, 
		bool bPrintTimer = false);

	namespace detail
	{
		extern graphics::PoolBufferGPUOnlyRef filterPipelineIndirectDispatchCmd(
			graphics::GraphicsQueue& queue,
			const GLTFRenderContext& renderCtx, 
			const std::string& name, 
			graphics::PoolBufferGPUOnlyRef countBuffer);


		extern void hzbCullingGeneric(
			graphics::GraphicsQueue& queue,
			const HZBContext& inHzb,
			uint instanceViewId,
			uint instanceViewOffset,
			bool bObjectUseLastFrameProject,
			const GLTFRenderContext& renderCtx,
			graphics::PoolBufferGPUOnlyRef inCountBuffer,
			graphics::PoolBufferGPUOnlyRef inCmdBuffer,
			CountAndCmdBuffer& outBuffer);

		extern CountAndCmdBuffer hzbCulling(
			graphics::GraphicsQueue& queue,
			const HZBContext& inHzb,
			const GLTFRenderContext& renderCtx, 
			bool bFirstStage, 
			graphics::PoolBufferGPUOnlyRef inCountBuffer, 
			graphics::PoolBufferGPUOnlyRef inCmdBuffer, 
			CountAndCmdBuffer& outBuffer);
	
		constexpr uint32 kNoCareTwoSideFlag = ~0U;
		extern CountAndCmdBuffer filterPipeForVisibility(
			graphics::GraphicsQueue& queue, 
			const GLTFRenderContext& renderCtx, 
			graphics::PoolBufferGPUOnlyRef dispatchCmd,
			graphics::PoolBufferGPUOnlyRef inCmdBuffer,
			graphics::PoolBufferGPUOnlyRef inCountBuffer, 
			uint alphaMode, 
			uint bTwoSide);
	}



	// Return true if need invoke stage1.
	// Return false no need stage1.
	extern bool gltfVisibilityRenderingStage0( 
		graphics::GraphicsQueue& queue,
		const GBufferTextures& gbuffers,
		const GLTFRenderContext& renderCtx, 
		const HZBContext& hzbCtx, 
		uint instanceViewId,
		uint instanceViewOffset,
		CountAndCmdBuffer inCountAndCmdBuffer, 
		CountAndCmdBuffer& outCountAndCmdBuffer);

	extern void gltfVisibilityRenderingStage1(
		graphics::GraphicsQueue& queue, 
		const GBufferTextures& gbuffers,
		const GLTFRenderContext& renderCtx, 
		const HZBContext& hzbCtx, 
		uint instanceViewId,
		uint instanceViewOffset,
		CountAndCmdBuffer inCountAndCmdBuffer);


	extern CascadeShadowContext renderShadow(
		graphics::CommandList& cmd,
		graphics::GraphicsQueue& queue,
		const GLTFRenderContext& renderCtx,
		const PerframeCameraView& cameraView,
		const CascadeShadowHistory& cascadeHistory,
		const std::vector<CascadeInfo>& cascadeInfo,
		const ShadowConfig& config,
		const ApplicationTickData& tickData,
		const ICamera& camera,
		const float3& lightDirection);

	extern CascadeShadowHistory extractCascadeShadowHistory(
		graphics::GraphicsQueue& queue,
		const float3& direction,
		const CascadeShadowContext& shadowCtx,
		const ApplicationTickData& tickData
	);
}