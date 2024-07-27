#include <renderer/fullscreen.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>

namespace chord
{ 
	using namespace graphics;
    IMPLEMENT_GLOBAL_SHADER(FullScreenVS, "resource/shader/fullscreen.hlsl", "fullScreenVS", EShaderStage::Vertex);



	void chord::addFullScreenPass(
		GraphicsQueue& queue, 
		const std::string& name, 
		GraphicsPipelineRef pipe, 
		RenderTargets RTs, 
		std::function<void(GraphicsQueue& queue, GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		auto& cmd = queue.getActiveCmd()->commandBuffer;
		RTs.beginRendering(queue);
		{
			pipe->bind(cmd);
			vkCmdSetCullMode(cmd, VK_CULL_MODE_BACK_BIT);
			// Set blend state, set vertex state, set viewport state here.
			lambda(queue, pipe, cmd);

			// Draw full screen triangle.
			vkCmdDraw(cmd, 3, 1, 0, 0);
		}
		RTs.endRendering(queue);
	}

	void chord::addComputePass(
		GraphicsOrComputeQueue& queue, 
		const std::string& name, 
		ComputePipelineRef pipe,
		math::uvec3 dispatchParameter,
		std::function<void(GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		queue.checkRecording();
		auto& cmd = queue.getActiveCmd()->commandBuffer;

		pipe->bind(cmd);
		lambda(queue, pipe, cmd);
		vkCmdDispatch(cmd, dispatchParameter.x, dispatchParameter.y, dispatchParameter.z);
	}

	void addIndirectComputePass(
		graphics::GraphicsOrComputeQueue& queue, 
		const std::string& name, 
		graphics::ComputePipelineRef pipe, 
		PoolBufferRef dispatchBuffer,
		VkDeviceSize offset,
		std::function<void(graphics::GraphicsOrComputeQueue& queue, graphics::ComputePipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		queue.checkRecording();

		queue.transition(dispatchBuffer, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		auto& cmd = queue.getActiveCmd()->commandBuffer;
		pipe->bind(cmd);
		lambda(queue, pipe, cmd);

		vkCmdDispatchIndirect(cmd, dispatchBuffer->get(), offset);
	}

	void addIndirectDrawPass(
		graphics::GraphicsQueue& queue, 
		const std::string& name, 
		graphics::GraphicsPipelineRef pipe,
		RenderTargets& RTs,
		PoolBufferRef cmdBuffer, VkDeviceSize offset, PoolBufferRef countBuffer, VkDeviceSize countBufferOffset,
		uint32_t maxDrawCount, uint32_t stride,
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		queue.checkRecording();

		queue.transition(cmdBuffer, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
		queue.transition(countBuffer, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		auto& cmd = queue.getActiveCmd()->commandBuffer;

		pipe->bind(cmd);

		RTs.beginRendering(queue);
		{
			vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

			lambda(queue, pipe, cmd);

			vkCmdDrawIndirectCount(cmd, cmdBuffer->get(), offset, countBuffer->get(), countBufferOffset, maxDrawCount, stride);
		}
		RTs.endRendering(queue);
	}

};