#pragma once
#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>

namespace chord
{
	extern void tonemapping(graphics::GraphicsQueue& queue, graphics::PoolTextureRef srcImage, graphics::PoolTextureRef outImage);
}