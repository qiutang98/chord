#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_helper.h>
#include <renderer/visibility_tile.h>
#include <renderer/render_textures.h>
#include <renderer/mesh/gltf_rendering.h>

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

	extern void lighting(
		graphics::GraphicsQueue& queue,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::PoolBufferGPUOnlyRef drawMeshletCmdBuffer,
		const VisibilityTileMarkerContext& marker);

	extern graphics::PoolTextureRef computeDisocclusionMask(
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
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::helper::AccelKHRRef tlas);
}