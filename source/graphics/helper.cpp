#include <graphics/helper.h>

namespace chord::graphics::helper
{
	const std::array<VkPipelineStageFlags, 1> SubmitInfo::kDefaultWaitStages =
	{
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
	};
}