#include <renderer/fullscreen.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>

namespace chord
{ 
	namespace graphics
	{
		IMPLEMENT_GLOBAL_SHADER(FullScreenVS, "resource/shader/fullscreen.hlsl", "fullScreenVS", EShaderStage::Vertex);
	}



	void chord::addFullScreenPass(
		graphics::GraphicsQueue& queue,
		const std::string& name, 
		graphics::GraphicsPipelineRef pipe,
		RenderTargets RTs, 
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		using namespace graphics;

		auto& cmd = queue.getActiveCmd()->commandBuffer;
		ScopePerframeMarker marker(queue, name.c_str());
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
		graphics::GraphicsOrComputeQueue& queue,
		const std::string& name, 
		graphics::ComputePipelineRef pipe,
		math::uvec3 dispatchParameter,
		std::function<void(graphics::GraphicsOrComputeQueue& queue, graphics::ComputePipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		using namespace graphics;

		queue.checkRecording();
		auto& cmd = queue.getActiveCmd()->commandBuffer;

		ScopePerframeMarker marker(queue, name.c_str());

		pipe->bind(cmd);
		lambda(queue, pipe, cmd);
		vkCmdDispatch(cmd, dispatchParameter.x, dispatchParameter.y, dispatchParameter.z);
	}

	void addIndirectComputePass(
		graphics::GraphicsOrComputeQueue& queue, 
		const std::string& name, 
		graphics::ComputePipelineRef pipe, 
		graphics::PoolBufferRef dispatchBuffer,
		VkDeviceSize offset,
		std::function<void(graphics::GraphicsOrComputeQueue& queue, graphics::ComputePipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		using namespace graphics;

		queue.checkRecording();
		ScopePerframeMarker marker(queue, name.c_str());

		queue.transition(dispatchBuffer, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		auto& cmd = queue.getActiveCmd()->commandBuffer;
		pipe->bind(cmd);
		lambda(queue, pipe, cmd);

		vkCmdDispatchIndirect(cmd, dispatchBuffer->get(), offset);
	}

	void addDrawPass(
		graphics::GraphicsQueue& queue, 
		const std::string& name, 
		graphics::GraphicsPipelineRef pipe, 
		RenderTargets& RTs, 
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		using namespace graphics;

		queue.checkRecording();
		auto& cmd = queue.getActiveCmd()->commandBuffer;
		ScopePerframeMarker marker(queue, name.c_str());

		pipe->bind(cmd);

		RTs.beginRendering(queue);
		{
			vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);
			lambda(queue, pipe, cmd);
		}
		RTs.endRendering(queue);
	}

	void addMeshIndirectDrawPass(
		graphics::GraphicsQueue& queue, 
		const std::string& name, 
		graphics::GraphicsPipelineRef pipe,
		RenderTargets& RTs, 
		graphics::PoolBufferRef cmdBuffer, 
		VkDeviceSize offset, 
		uint32 stride, 
		uint32 drawCount,
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		using namespace graphics;

		queue.checkRecording();
		ScopePerframeMarker marker(queue, name.c_str());
		queue.transition(cmdBuffer, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		auto& cmd = queue.getActiveCmd()->commandBuffer;

		RTs.beginRendering(queue);
		{
			pipe->bind(cmd);
			lambda(queue, pipe, cmd);
			vkCmdDrawMeshTasksIndirectEXT(cmd, cmdBuffer->get(), offset, drawCount, stride);
		}
		RTs.endRendering(queue);
	}

	void addMeshIndirectCountDrawPass(
		graphics::GraphicsQueue& queue, 
		const std::string& name, 
		graphics::GraphicsPipelineRef pipe, 
		RenderTargets& RTs, 
		graphics::PoolBufferRef cmdBuffer, 
		VkDeviceSize offset,
		graphics::PoolBufferRef countBuffer, 
		VkDeviceSize countBufferOffset, 
		uint32_t maxDrawCount, 
		uint32_t stride, 
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		using namespace graphics;

		queue.checkRecording();
		ScopePerframeMarker marker(queue, name.c_str());

		queue.transition(cmdBuffer,   VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
		queue.transition(countBuffer, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

		auto& cmd = queue.getActiveCmd()->commandBuffer;
		RTs.beginRendering(queue);
		{
			pipe->bind(cmd);
			lambda(queue, pipe, cmd);
			vkCmdDrawMeshTasksIndirectCountEXT(cmd, cmdBuffer->get(), offset, countBuffer->get(), countBufferOffset, maxDrawCount, stride);
		}
		RTs.endRendering(queue);
	}

	void addIndirectDrawPass(
		graphics::GraphicsQueue& queue, 
		const std::string& name, 
		graphics::GraphicsPipelineRef pipe,
		RenderTargets& RTs,
		graphics::PoolBufferRef cmdBuffer, VkDeviceSize offset, graphics::PoolBufferRef countBuffer, VkDeviceSize countBufferOffset,
		uint32_t maxDrawCount, uint32_t stride,
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
	{
		using namespace graphics;

		queue.checkRecording();
		ScopePerframeMarker marker(queue, name.c_str());
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