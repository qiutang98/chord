#include <renderer/fullscreen.h>

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

};