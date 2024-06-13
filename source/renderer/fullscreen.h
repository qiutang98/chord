#pragma once

#include <shader/shader.h>
#include <shader/compiler.h>

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/render_helper.h>


namespace chord
{
    DECLARE_GLOBAL_SHADER(FullScreenVS);

	extern void addFullScreenPass(
		graphics::GraphicsQueue& queue,
		const std::string& name,
		graphics::GraphicsPipelineRef pipe,
		RenderTargets RTs,
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda);

	template<typename PixelShader>
    static inline void addFullScreenPass(
		graphics::GraphicsQueue& queue, 
		const std::string& name, 
		RenderTargets RTs,
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda)
    {
		using namespace graphics;
		queue.checkRecording();

		auto graphicsPipeline = getContext().graphicsPipe<FullScreenVS, PixelShader>(
			name, 
			std::move(RTs.getRTsFormats()), 
			RTs.getDepthStencilFormat(), 
			RTs.getDepthStencilFormat());

		addFullScreenPass(queue, name, std::dynamic_pointer_cast<GraphicsPipeline>(graphicsPipeline), RTs, std::move(lambda));
    }
}