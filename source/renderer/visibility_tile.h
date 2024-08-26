#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_helper.h>

namespace chord
{
	struct VisibilityTileMarkerContext
	{
		graphics::PoolTextureRef visibilityTexture;
		graphics::PoolTextureRef markerTexture;

		uint2 visibilityDim;
		uint2 markerDim;
	};



	extern VisibilityTileMarkerContext visibilityMark(
		graphics::GraphicsQueue& queue, 
		uint cameraViewId,
		graphics::PoolBufferGPUOnlyRef drawMeshletCmdBuffer,
		graphics::PoolTextureRef visibilityImage);

	struct VisibilityTileContxt
	{
		graphics::PoolBufferGPUOnlyRef tileCmdBuffer;
		graphics::PoolBufferGPUOnlyRef dispatchIndirectBuffer;
	};
	VisibilityTileContxt prepareShadingTileParam(
		graphics::GraphicsQueue& queue, EShadingType shadingType, const VisibilityTileMarkerContext& marker);
}