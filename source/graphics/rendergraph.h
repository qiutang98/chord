#pragma once

#include <graphics/common.h>
#include <graphics/rendertargetpool.h>
#include <graphics/bufferpool.h>
#include <graphics/graphics.h>

namespace chord::graphics
{
	// We use bindless system, and buffer/texture's life managed by pool.
	// so the render graph only collect resource and pass, insert barrier correctly.
	class RenderGraph : NonCopyable
	{
	public:


		void addPass();


	private:



	};
}
