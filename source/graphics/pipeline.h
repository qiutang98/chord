#pragma once

#include <utils/utils.h>

namespace chord::graphics
{
	class GraphicsPipelineCreateInfo;

	// Vulkan pipeline wrapper.
	class IPipeline : NonCopyable
	{
	public:
		explicit IPipeline(const std::string& name, uint32 pushConstSize, VkShaderStageFlags shaderStageFlags);
		virtual ~IPipeline();

		virtual void bind(VkCommandBuffer cmd) const = 0;

		auto getPipelineLayout() const 
		{ 
			return m_pipelineLayout; 
		}

		template<typename T>
		void pushConst(VkCommandBuffer cmd, const T& data) const
		{
			check(sizeof(data) <= m_pushConstSize);
			vkCmdPushConstants(cmd, m_pipelineLayout, m_shaderStageFlags, 0, sizeof(data), &data);
		}

	protected:
		void initPipeline(VkPipeline pipeline);

	protected:
		std::string m_name;
		VkShaderStageFlags m_shaderStageFlags;
		uint32 m_pushConstSize = 0;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	};
	using IPipelineRef = std::shared_ptr<IPipeline>;

	class GraphicsPipeline final : public IPipeline
	{
	public:
		explicit GraphicsPipeline(const std::string& name, const GraphicsPipelineCreateInfo& ci);
		~GraphicsPipeline();

		virtual void bind(VkCommandBuffer cmd) const override;
	};

	// Container collect pipeline when require.
	class PipelineContainer : NonCopyable
	{
	public:
		IPipelineRef graphics(const std::string& name, const GraphicsPipelineCreateInfo& ci);

	private:
		std::map<uint64, std::shared_ptr<GraphicsPipeline>> m_graphics;
	};
}