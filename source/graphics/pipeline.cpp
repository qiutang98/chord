#include <graphics/graphics.h>
#include <graphics/pipeline.h>
#include <graphics/helper.h>
#include <utils/cityhash.h>
#include <shader_compiler/shader.h>

#define COMMON_GRAPHICS_PIPELINE_DYNAMIC_STATES \
	VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT, \
	VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT,    \
	VK_DYNAMIC_STATE_POLYGON_MODE_EXT,          \
	VK_DYNAMIC_STATE_CULL_MODE,                 \
	VK_DYNAMIC_STATE_FRONT_FACE,                \
	VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,         \
	VK_DYNAMIC_STATE_DEPTH_BIAS,                \
	VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,       \
	VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,        \
	VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,         \
	VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,        \
	VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,          \
	VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,  \
	VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,       \
	VK_DYNAMIC_STATE_STENCIL_OP,                \
	VK_DYNAMIC_STATE_DEPTH_BOUNDS,              \
	VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT,       \
	VK_DYNAMIC_STATE_LOGIC_OP_EXT,              \
	VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,    \
	VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,  \
	VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,      \
	VK_DYNAMIC_STATE_BLEND_CONSTANTS

namespace chord::graphics
{


	static const std::vector<VkDynamicState> kGraphicsPipelineDynamicStates =
	{
		COMMON_GRAPHICS_PIPELINE_DYNAMIC_STATES,
		VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
	};

	static const std::vector<VkDynamicState> kGraphicsPipelineDynamicStatesMeshPipe =
	{
		COMMON_GRAPHICS_PIPELINE_DYNAMIC_STATES
	};

	IPipeline::IPipeline(
		const std::string& name, 
		uint32 pushConstSize, 
		VkShaderStageFlags shaderStageFlags)
		: m_name(name)
		, m_pushConstSize(pushConstSize)
		, m_shaderStageFlags(shaderStageFlags)
	{

	}

	IPipeline::~IPipeline()
	{
		if (m_pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(getDevice(), m_pipeline, getAllocationCallbacks());
		}
		m_pipeline = VK_NULL_HANDLE;
	}

	void IPipeline::initPipeline(VkPipeline pipeline)
	{
		check(m_pipeline == VK_NULL_HANDLE);

		m_pipeline = pipeline;
		setResourceName(VK_OBJECT_TYPE_PIPELINE, (uint64)m_pipeline, m_name.c_str());
	}

	ComputePipeline::ComputePipeline(const std::string& name, const ComputePipelineCreateInfo& ci)
		: IPipeline(name, ci.pushConstantSize, VK_SHADER_STAGE_COMPUTE_BIT)
	{
		VkPushConstantRange pushConstantRange{};
		uint32 pushConstCount = 0;
		if (ci.pushConstantSize > 0)
		{
			pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushConstantRange.size = ci.pushConstantSize;
			pushConstCount = 1;
		}

		std::vector<VkDescriptorSetLayout> descriptorSetLayouts { };
		descriptorSetLayouts.push_back(getContext().getBindlessManger().getSetLayout());
		for (const auto& setLayout : ci.additionalDescriptorSetLayouts)
		{
			descriptorSetLayouts.push_back(setLayout);
		}

		m_pipelineLayout = getContext().getPipelineLayoutManager().getLayout(descriptorSetLayouts.size(), descriptorSetLayouts.data(), pushConstCount, &pushConstantRange);

		VkPipelineShaderStageCreateInfo shaderStageCI{};
		shaderStageCI.module = ci.shaderModule;
		shaderStageCI.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStageCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		shaderStageCI.pName = ci.shaderEntryName.c_str();

		VkComputePipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineCI.layout = m_pipelineLayout;
		pipelineCI.flags = 0;
		pipelineCI.stage = shaderStageCI;

		VkPipeline pipeline;
		checkVkResult(vkCreateComputePipelines(getDevice(), getContext().getPipelineCache(), 1, &pipelineCI, getContext().getAllocationCallbacks(), &pipeline));

		// Assign in pipeline.
		IPipeline::initPipeline(pipeline);
	}

	GraphicsPipeline::GraphicsPipeline(const std::string& name, const GraphicsPipelineCreateInfo& ci)
		: IPipeline(name, ci.pushConstantSize, ci.shaderStageFlags)
	{
		// Dynamic render pass state.
		VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{ };
		pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		pipelineRenderingCreateInfo.colorAttachmentCount = static_cast<uint32>(ci.attachmentFormats.size());
		pipelineRenderingCreateInfo.pColorAttachmentFormats = ci.attachmentFormats.data();
		pipelineRenderingCreateInfo.depthAttachmentFormat   = ci.depthFormat;
		pipelineRenderingCreateInfo.stencilAttachmentFormat = ci.stencilFormat;

		// Dynamic state.
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

		const auto& dynamicStates = hasFlag(ci.shaderStageFlags, VK_SHADER_STAGE_MESH_BIT_EXT) ?
			kGraphicsPipelineDynamicStatesMeshPipe : kGraphicsPipelineDynamicStates;

		dynamicState.dynamicStateCount = static_cast<uint32>(dynamicStates.size());
		dynamicState.pDynamicStates    = dynamicStates.data();

		// Add push const if exist.
		VkPushConstantRange pushConstantRange{};
		uint32 pushConstCount = 0;
		if (ci.pushConstantSize > 0)
		{
			pushConstantRange.stageFlags = ci.shaderStageFlags;
			pushConstantRange.size       = ci.pushConstantSize;
			pushConstCount = 1;
		}

		VkPipelineInputAssemblyStateCreateInfo assemblyState{ };
		assemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assemblyState.topology = ci.topology;
		assemblyState.primitiveRestartEnable = VK_FALSE; // If need dynamic, add VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE.

		// Rasterize states, dynamic setting.
		VkPipelineRasterizationStateCreateInfo rasterizeState{};
		rasterizeState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizeState.rasterizerDiscardEnable = VK_FALSE;
		rasterizeState.lineWidth = 1.0f;

		// MSAA state, dynamic setting.
		VkPipelineMultisampleStateCreateInfo multisampleState{};
		multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		std::vector<VkPipelineShaderStageCreateInfo> pipelineStages(ci.pipelineStages.size());
		for (int32 i = 0; i < ci.pipelineStages.size(); i++)
		{
			pipelineStages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			pipelineStages[i].flags = ci.pipelineStages.data()[i].flags;
			pipelineStages[i].stage = ci.pipelineStages.data()[i].stage;
			pipelineStages[i].module = ci.pipelineStages.data()[i].module;
			pipelineStages[i].pName = ci.pipelineStages.data()[i].pName;
		}

		VkGraphicsPipelineCreateInfo createInfo{ };
		createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		createInfo.pNext = &pipelineRenderingCreateInfo;
		createInfo.stageCount = static_cast<uint32>(pipelineStages.size());
		createInfo.pStages = pipelineStages.data(); // 
		createInfo.pInputAssemblyState = &assemblyState;
		createInfo.pMultisampleState = &multisampleState;
		createInfo.pRasterizationState = &rasterizeState;
		createInfo.pDynamicState = &dynamicState;

		// Pipeline layout create from cache.
		m_pipelineLayout = getContext().getPipelineLayoutManager().getLayout(1, &getContext().getBindlessManger().getSetLayout(), pushConstCount, &pushConstantRange);
		createInfo.layout = m_pipelineLayout;

		VkPipeline pipeline;
		checkVkResult(vkCreateGraphicsPipelines(getDevice(), getContext().getPipelineCache(), 1, &createInfo, getAllocationCallbacks(), &pipeline));
		
		// Assign in pipeline.
		IPipeline::initPipeline(pipeline);
	}

	GraphicsPipeline::~GraphicsPipeline()
	{
		
	}

	void GraphicsPipeline::bind(VkCommandBuffer cmd) const
	{
		check(m_pipeline != VK_NULL_HANDLE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		getBindless().bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout);
	}

	GraphicsPipelineRef PipelineContainer::graphics(const std::string& name, const GraphicsPipelineCreateInfo& ci)
	{
		uint64 hash = ci.hash();
		if (m_graphics[hash] == nullptr)
		{
			m_graphics[hash] = std::make_shared<GraphicsPipeline>(name, ci);
		}
		return m_graphics[hash];
	}

	ComputePipelineRef PipelineContainer::compute(const std::string& name, const ComputePipelineCreateInfo& ci)
	{
		uint64 hash = ci.hash();
		if (m_computes[hash] == nullptr)
		{
			m_computes[hash] = std::make_shared<ComputePipeline>(name, ci);
		}
		return m_computes[hash];
	}

	void ComputePipeline::bind(VkCommandBuffer cmd) const
	{
		check(m_pipeline != VK_NULL_HANDLE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
		getBindless().bind(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout);
	}

	ComputePipeline::~ComputePipeline()
	{

	}
}

