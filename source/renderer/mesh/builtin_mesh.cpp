#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/mesh_raster.hlsl>
#include <shader/instance_culling.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/postprocessing/postprocessing.h>
#include <scene/system/shadow.h>
#include <renderer/renderer.h>
#include <shader/builtin_mesh_draw.hlsl>

using namespace chord;
using namespace chord::graphics;

PRIVATE_GLOBAL_SHADER(BuiltinMeshDrawVS, "resource/shader/builtin_mesh_draw.hlsl", "builtinMeshVS", EShaderStage::Vertex);
PRIVATE_GLOBAL_SHADER(BuiltinMeshDrawPS, "resource/shader/builtin_mesh_draw.hlsl", "builtinMeshPS", EShaderStage::Pixel);


void chord::debugDrawBuiltinMesh(
	graphics::GraphicsQueue& queue, 
	std::vector<BuiltinMeshDrawInstance>& instances, 
	uint32 cameraViewId,
	graphics::PoolTextureRef depthImage, 
	graphics::PoolTextureRef outImage)
{
	ScopePerframeMarker marker(queue, "BuiltinInstanceMeshDraw");

	std::ranges::sort(instances, [](const auto& a, const auto& b)
	{
		return a.mesh->meshTypeUniqueId < b.mesh->meshTypeUniqueId;
	});

	BuiltinMeshDrawPushConst pushConsts{};
	pushConsts.cameraViewId = cameraViewId;

	auto vertexShader = getContext().getShaderLibrary().getShader<BuiltinMeshDrawVS>();
	auto pixelShader = getContext().getShaderLibrary().getShader<BuiltinMeshDrawPS>();

	RenderTargets RTs{ };
	{
		RTs.RTs[0] = RenderTargetRT(outImage, ERenderTargetLoadStoreOp::Load_Store);
		RTs.depthStencil = DepthStencilRT(
			depthImage,
			EDepthStencilOp::DepthWrite_StnecilRead,
			ERenderTargetLoadStoreOp::Load_Nope);
	}

	auto pipeline = getContext().graphicsPipe(
		vertexShader, 
		pixelShader,
		"debugDrawBuiltinMesh-Pipe",
		std::move(RTs.getRTsFormats()),
		RTs.getDepthFormat(),
		RTs.getStencilFormat(),
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	addDrawPass(queue, "BuiltinMeshDraw", pipeline, RTs,
		[&](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
		{

			const auto binding = helper::vertexInputBindingDescription2EXT(sizeof(BuiltinMesh::BuiltinVertex));
			const VkVertexInputAttributeDescription2EXT attributes[3] =
			{
				helper::vertexInputAttributeDescription2EXT(0, VK_FORMAT_R32G32B32_SFLOAT,  offsetof(BuiltinMesh::BuiltinVertex, position), binding.binding),
				helper::vertexInputAttributeDescription2EXT(1, VK_FORMAT_R32G32B32_SFLOAT,  offsetof(BuiltinMesh::BuiltinVertex, normal),  binding.binding),
				helper::vertexInputAttributeDescription2EXT(2, VK_FORMAT_R32G32_SFLOAT,     offsetof(BuiltinMesh::BuiltinVertex, uv), binding.binding),
			};

			vkCmdSetVertexInputEXT(cmd, 1, &binding, countof(attributes), attributes);

			vkCmdSetDepthTestEnable(cmd, VK_TRUE);
			vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
			vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_GREATER_OR_EQUAL);

			// Debug draw don't care performances.
			BuiltinMesh* cacheMesh = nullptr;

			for (const auto& instance : instances)
			{
				if (cacheMesh != instance.mesh)
				{
					cacheMesh = instance.mesh;

					// Bind indexing buffer and vertex buffer.
					queue.bindIndexBuffer(cacheMesh->indices, 0);
					queue.bindVertexBuffer(cacheMesh->vertices);
				}

				pushConsts.color = instance.color;
				pushConsts.offset = instance.offset;
				pushConsts.scale = instance.scale;

				pipe->pushConst(cmd, pushConsts);
				vkCmdDrawIndexed(cmd, cacheMesh->indicesCount, 1, 0, 0, 0);
			}
		});
}