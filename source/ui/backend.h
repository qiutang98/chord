#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>

namespace chord::ui
{
	class Backend : NonCopyable
	{
	public:
		void init();
		void release();

	private:
		std::vector<VkCommandPool>   m_commandPools;
		std::vector<VkCommandBuffer> m_commandBuffers;
	};
}