#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/command.h>


namespace chord::graphics
{
	CommandBuffer::~CommandBuffer()
	{
		// Destroy pool.
		helper::destroyCommandPool(pool);
	}

}

