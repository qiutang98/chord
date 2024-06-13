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
			vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);
			pipe->bind(cmd);

			// Set blend state, set vertex state, set viewport state here.
			lambda(queue, pipe, cmd);

			// Draw full screen triangle.
			vkCmdDraw(cmd, 3, 1, 0, 0);
		}
		RTs.endRendering(queue);
	}

};