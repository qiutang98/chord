#pragma once

#include <shader_compiler/shader.h>
#include <shader_compiler/compiler.h>

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/render_helper.h>


namespace chord
{
	extern void addComputePass(
		graphics::GraphicsOrComputeQueue& queue,
		const std::string& name,
		graphics::ComputePipelineRef pipe,
		math::uvec3 dispatchParameter,
		std::function<void(graphics::GraphicsOrComputeQueue& queue, graphics::ComputePipelineRef pipe, VkCommandBuffer cmd)>&& lambda
	);

	template<typename PushConstType>
	static inline void addComputePass2(
		graphics::GraphicsOrComputeQueue& queue,
		const std::string& name,
		graphics::ComputePipelineRef pipe,
		const PushConstType& pushConst,
		math::uvec3 dispatchParameter)
	{
		addComputePass(queue, name, pipe, dispatchParameter, [pushConst](graphics::GraphicsOrComputeQueue& queue, graphics::ComputePipelineRef pipe, VkCommandBuffer cmd)
		{
			pipe->pushConst(cmd, pushConst);
		});
	}

	extern void addIndirectComputePass(
		graphics::GraphicsOrComputeQueue& queue,
		const std::string& name,
		graphics::ComputePipelineRef pipe,
		graphics::PoolBufferRef dispatchBuffer,
		VkDeviceSize offset,
		std::function<void(graphics::GraphicsOrComputeQueue& queue, graphics::ComputePipelineRef pipe, VkCommandBuffer cmd)>&& lambda
	);

	template<typename PushConstType>
	void addIndirectComputePass2(
		graphics::GraphicsOrComputeQueue& queue,
		const std::string& name,
		graphics::ComputePipelineRef pipe,
		const PushConstType& pushConst,
		graphics::PoolBufferRef dispatchBuffer,
		VkDeviceSize offset = 0)
	{
		addIndirectComputePass(queue, name, pipe, dispatchBuffer, offset, [pushConst](graphics::GraphicsOrComputeQueue& queue, graphics::ComputePipelineRef pipe, VkCommandBuffer cmd)
		{
			pipe->pushConst(cmd, pushConst);
		});
	}
}