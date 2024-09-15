#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/render_helper.h>
#include <astrophysics/atmosphere.h>

struct LineDrawVertex;

namespace chord
{


	extern void tonemapping(graphics::GraphicsQueue& queue, graphics::PoolTextureRef srcImage, graphics::PoolTextureRef outImage);
	
	struct DebugLineCtx
	{
		uint32 cameraViewBufferId;
		std::vector<LineDrawVertex> vertices;

		graphics::PoolBufferRef gpuVertices;
		graphics::PoolBufferRef gpuCountBuffer;
		uint32 gpuMaxCount;

		void prepareForRender(graphics::GraphicsQueue& queue);
	};
	extern DebugLineCtx allocateDebugLineCtx();

	extern void debugLine(
		graphics::GraphicsQueue& queue, 
		const DebugLineCtx& ctx,
		graphics::PoolTextureRef depthImage,
		graphics::PoolTextureRef outImage);
	extern HZBContext buildHZB(
		graphics::GraphicsQueue& queue, 
		graphics::PoolTextureRef depthImage,
		bool bBuildMin,
		bool bBuildMax);


	extern void renderSky(
		graphics::GraphicsQueue& queue,
		graphics::PoolTextureRef sceneColor,
		uint32 cameraViewId,
		const AtmosphereLut& luts);
}