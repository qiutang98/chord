#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_textures.h>
#include <scene/scene_common.h>
#include <renderer/postprocessing/postprocessing.h>

namespace chord
{
	struct GLTFRenderContext
	{
		struct PostBasicCulling
		{
			// Visible meshlet count buffer id.
			graphics::PoolBufferGPUOnlyRef meshletCountBuffer;

			// Visible meshlet draw cmd buffer id.
			graphics::PoolBufferGPUOnlyRef meshletCmdBuffer;

			// Stage count and cmd buffer.
			graphics::PoolBufferGPUOnlyRef meshletCountBufferStage;
			graphics::PoolBufferGPUOnlyRef meshletCmdBufferStage;
		} postBasicCullingCtx;

		const PerframeCollected* perframeCollect;
		const uint gltfObjectCount;
		const uint gltfBufferId;
		const uint cameraView;

		graphics::GraphicsQueue& queue;
		GBufferTextures& gbuffers;
		const DeferredRendererHistory& history;

		GLTFRenderContext(
			const PerframeCollected* inPerframeCollected,
			const uint inGLTFObjectCount,
			const uint inGLTFBufferId,
			const uint inCameraViewId,
			graphics::GraphicsQueue& inQueue,
			GBufferTextures& inGBuffers,
			const DeferredRendererHistory& inHistory)
			: perframeCollect(inPerframeCollected)
			, gltfObjectCount(inGLTFObjectCount)
			, gltfBufferId(inGLTFBufferId)
			, cameraView(inCameraViewId)
			, queue(inQueue)
			, gbuffers(inGBuffers)
			, history(inHistory)
		{

		}
	};

	// Return true if need invoke stage1.
	// Return false no need stage1.
	extern bool gltfVisibilityRenderingStage0(GLTFRenderContext& renderCtx);
	extern void gltfVisibilityRenderingStage1(GLTFRenderContext& renderCtx, const HZBContext& hzbCtx);
}