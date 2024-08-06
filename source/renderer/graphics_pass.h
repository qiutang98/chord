#pragma once

#include <shader/shader.h>
#include <shader/compiler.h>

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/render_helper.h>


namespace chord
{
	extern void addDrawPass(
		graphics::GraphicsQueue& queue,
		const std::string& name,
		graphics::GraphicsPipelineRef pipe,
		RenderTargets& RTs,
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda);

	extern void addIndirectDrawPass(
		graphics::GraphicsQueue& queue,
		const std::string& name,
		graphics::GraphicsPipelineRef pipe,
		RenderTargets& RTs,
		graphics::PoolBufferRef cmdBuffer, VkDeviceSize offset, graphics::PoolBufferRef countBuffer, VkDeviceSize countBufferOffset,
		uint32_t maxDrawCount, uint32_t stride,
		std::function<void(graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)>&& lambda);

	template<typename PushConstType>
	static inline void addIndirectDrawPass2(
		graphics::GraphicsQueue& queue,
		const std::string& name,
		graphics::GraphicsPipelineRef pipe,
		RenderTargets& RTs,
		graphics::PoolBufferRef cmdBuffer, VkDeviceSize offset, graphics::PoolBufferRef countBuffer, VkDeviceSize countBufferOffset,
		uint32_t maxDrawCount, uint32_t stride,
		const PushConstType& push)
	{
		addIndirectDrawPass(queue, name, pipe, RTs, cmdBuffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride,
			[push](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
			{
				pipe->pushConst(cmd, push);
			});

	}
}