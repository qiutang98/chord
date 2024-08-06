#include <graphics/common.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <utils/cityhash.h>

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


	PipelineLayoutManager::~PipelineLayoutManager()
	{
		clear();
	}

	VkPipelineLayout PipelineLayoutManager::getLayout(
		uint32 setLayoutCount,
		const VkDescriptorSetLayout* pSetLayouts,
		uint32 pushConstantRangeCount,
		const VkPushConstantRange* pPushConstantRanges)
	{
		VkPipelineLayoutCreateInfo ci{ };
		ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

		ci.setLayoutCount = setLayoutCount;
		ci.pSetLayouts = pSetLayouts;
		ci.pushConstantRangeCount = pushConstantRangeCount;
		ci.pPushConstantRanges = pPushConstantRanges;

		uint64 hash = hashCombine(ci.setLayoutCount, ci.flags);
		for (int32 i = 0; i < ci.setLayoutCount; i++)
		{
			hash = hashCombine(hash, (uint64)ci.pSetLayouts[i]);
		}

		hash = hashCombine(hash, ci.pushConstantRangeCount);
		for (int32 i = 0; i < ci.pushConstantRangeCount; i++)
		{
			const auto& pushConstRange = ci.pPushConstantRanges[i];
			hash = hashCombine(hash, cityhash::cityhash64((const char*)(&pushConstRange), sizeof(pushConstRange)));
		}

		if (!m_layouts[hash].isValid())
		{
			VkPipelineLayout layout;
			checkVkResult(vkCreatePipelineLayout(getDevice(), &ci, getAllocationCallbacks(), &layout));

			m_layouts[hash] = layout;

			if (m_layouts.size() % 1000 == 0)
			{
				LOG_TRACE("Pipeline layouts increment already reach {}...", m_layouts.size());
			}
		}

		return m_layouts[hash].get();
	}

	void PipelineLayoutManager::clear()
	{
		for (auto& pair : m_layouts)
		{
			if (pair.second.isValid())
			{
				vkDestroyPipelineLayout(getDevice(), pair.second.get(), getAllocationCallbacks());
				pair.second = {};
			}
		}

		m_layouts.clear();
	}
}


