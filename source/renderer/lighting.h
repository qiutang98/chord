#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_helper.h>
#include <renderer/visibility_tile.h>
#include <renderer/render_textures.h>

namespace chord
{
	extern void lighting(
		graphics::GraphicsQueue& queue, 
		GBufferTextures& gbuffers, 
		uint32 cameraViewId,
		const VisibilityTileMarkerContext& marker);
}