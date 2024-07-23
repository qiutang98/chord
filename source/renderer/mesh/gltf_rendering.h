#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_textures.h>
#include <scene/scene_common.h>

namespace chord
{
	struct GLTFRenderDescriptor
	{
		const PerframeCollected* perframeCollect;
		uint objectCount;
		uint bufferId;
	};

	extern void gltfBasePassRendering(
		graphics::GraphicsQueue& queue, 
		GBufferTextures& gbuffers, 
		uint32 cameraView,
		const GLTFRenderDescriptor& gltfRenderDescriptor);
}