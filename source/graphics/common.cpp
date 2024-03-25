#include <graphics/common.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>

namespace chord::graphics
{
	CommandPoolResetable::CommandPoolResetable(const std::string& name)
	{
		// Create commandpool RESET bit per family.
		m_family = getContext().getQueuesInfo().graphicsFamily.get();
		m_pool = helper::createCommandPool(m_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

		setResourceName(VK_OBJECT_TYPE_COMMAND_POOL, (uint64)m_pool, name.c_str());
	}

	CommandPoolResetable::~CommandPoolResetable()
	{
		helper::destroyCommandPool(m_pool);
	}
}


