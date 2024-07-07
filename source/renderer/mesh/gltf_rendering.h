#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_textures.h>

namespace chord
{
	extern void gltfBasePassRendering(
		graphics::GraphicsQueue& queue, 
		GBufferTextures& gbuffers, 
		uint32 cameraView);
}