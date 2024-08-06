#pragma once

#include <utils/utils.h>

namespace chord::graphics
{
	class GraphicsPipelineCreateInfo;
	class ComputePipelineCreateInfo;

	// Vulkan pipeline wrapper.
	class IPipeline : NonCopyable
	{
	public:
		explicit IPipeline(
			const std::string& name, 
			uint32 pushConstSize, 
			VkShaderStageFlags shaderStageFlags);
		virtual ~IPipeline();

		virtual void bind(VkCommandBuffer cmd) const = 0;

		auto getPipelineLayout() const 
		{ 
			return m_pipelineLayout; 
		}

		template<typename T>
		void pushConst(VkCommandBuffer cmd, const T& data) const
		{
			static_assert(std::is_object_v<T> && !std::is_pointer_v<T>, "Type must be an object and not a pointer.");

			if (m_pushConstSize > 0)
			{
				check(sizeof(data) <= m_pushConstSize);
				vkCmdPushConstants(cmd, m_pipelineLayout, m_shaderStageFlags, 0, sizeof(data), &data);
			}
		}

		void pushConst(VkCommandBuffer cmd, const void* data, VkDeviceSize size)
		{
			if (m_pushConstSize > 0)
			{
				check(size <= m_pushConstSize);
				vkCmdPushConstants(cmd, m_pipelineLayout, m_shaderStageFlags, 0, size, data);
			}
		}

		VkPipelineLayout getLayout() const
		{
			return m_pipelineLayout;
		}

	protected:
		void initPipeline(VkPipeline pipeline);

	protected:
		// Pipeline name.
		std::string m_name;

		// Combine shader stages.
		VkShaderStageFlags m_shaderStageFlags;

		// Shared push const size.
		uint32 m_pushConstSize = 0;

		// Pipeline and pipeline layout.
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	};
	using IPipelineRef = std::shared_ptr<IPipeline>;

	class ComputePipeline final : public IPipeline
	{
	public:
		explicit ComputePipeline(const std::string& name, const ComputePipelineCreateInfo& ci);
		virtual ~ComputePipeline();

		virtual void bind(VkCommandBuffer cmd) const override;
	};
	using ComputePipelineRef = std::shared_ptr<ComputePipeline>;

	class GraphicsPipeline final : public IPipeline
	{
	public:
		explicit GraphicsPipeline(const std::string& name, const GraphicsPipelineCreateInfo& ci);
		virtual ~GraphicsPipeline();

		virtual void bind(VkCommandBuffer cmd) const override;
	};
	using GraphicsPipelineRef = std::shared_ptr<GraphicsPipeline>;

	// Container collect pipeline when require.
	class PipelineContainer : NonCopyable
	{
	public:
		GraphicsPipelineRef graphics(const std::string& name, const GraphicsPipelineCreateInfo& ci);
		ComputePipelineRef compute(const std::string& name, const ComputePipelineCreateInfo& ci);

	private:
		// Graphics pipeline map, hash by GraphicsPipelineCreateInfo.
		std::map<uint64, std::shared_ptr<GraphicsPipeline>> m_graphics;
		std::map<uint64, std::shared_ptr<ComputePipeline>> m_computes; 
	};
}