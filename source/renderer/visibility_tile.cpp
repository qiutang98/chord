#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/visibility_tile.h>
#include <shader/visibility_tile.hlsl>


namespace chord
{
	using namespace graphics;

	PRIVATE_GLOBAL_SHADER(VisibilityTileMarkerCS, "resource/shader/visibility_tile.hlsl", "tilerMarkerCS", EShaderStage::Compute);
	PRIVATE_GLOBAL_SHADER(VisibilityTilePrepareCS, "resource/shader/visibility_tile.hlsl", "tilePrepareCS", EShaderStage::Compute);
	PRIVATE_GLOBAL_SHADER(VisibilityTileCmdFillCS, "resource/shader/visibility_tile.hlsl", "prepareTileParamCS", EShaderStage::Compute);

	VisibilityTileMarkerContext visibilityMark(
		GraphicsQueue& queue, 
		uint cameraViewId,
		PoolBufferGPUOnlyRef drawMeshletCmdBuffer,
		PoolTextureRef visibilityImage)
	{
		VisibilityTileMarkerContext ctx{};
		ctx.visibilityTexture = visibilityImage;
		ctx.visibilityDim = { visibilityImage->get().getExtent().width, visibilityImage->get().getExtent().height };
		ctx.markerDim = divideRoundingUp(ctx.visibilityDim, uint2(8));
		ctx.markerTexture = getContext().getTexturePool().create(
			"TileMarkerTexture",
			ctx.markerDim.x,
			ctx.markerDim.y,
			VK_FORMAT_R32G32B32A32_UINT,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		VisibilityTilePushConsts pushConst { };

		pushConst.visibilityTexelSize = math::vec2(1.0f) / math::vec2(ctx.visibilityDim);
		pushConst.markerDim           = ctx.markerDim;
		pushConst.visibilityId        = asSRV(queue, visibilityImage);
		pushConst.markerTextureId     = asUAV(queue, ctx.markerTexture);
		pushConst.gatherSampler       = getContext().getSamplerManager().pointClampEdge().index.get();
		pushConst.cameraViewId        = cameraViewId;
		pushConst.drawedMeshletCmdId  = asSRV(queue, drawMeshletCmdBuffer);
		auto computeShader = getContext().getShaderLibrary().getShader<VisibilityTileMarkerCS>();
		addComputePass2(
			queue, 
			"VisibilityTileMarker CS", 
			getContext().computePipe(computeShader, "VisibilityTileMarkerPipe"),
			pushConst,
			math::uvec3((ctx.markerDim.x + 3) / 4, (ctx.markerDim.y + 3) / 4, 1));

		return ctx;
	}

	VisibilityTileContxt prepareShadingTileParam(GraphicsQueue& queue, EShadingType shadingType, const VisibilityTileMarkerContext& marker)
	{
		VisibilityTileContxt ctx{};
		ctx.tileCmdBuffer = getContext().getBufferPool().createGPUOnly("shadingTileCmd", sizeof(math::uvec2) * marker.markerDim.x * marker.markerDim.y, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		ctx.dispatchIndirectBuffer = getContext().getBufferPool().createGPUOnly("TileIndirectCmd", sizeof(math::uvec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
		
		auto countBuffer = getContext().getBufferPool().createGPUOnly("count", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		queue.clearUAV(countBuffer);

		VisibilityTilePushConsts pushConst{ };
		pushConst.visibilityTexelSize = math::vec2(1.0f) / math::vec2(marker.visibilityDim);
		pushConst.markerDim           = marker.markerDim;
		pushConst.visibilityId        = asSRV(queue, marker.visibilityTexture);
		pushConst.markerTextureId     = asSRV(queue, marker.markerTexture);
		pushConst.markerIndex         = uint(shadingType) / 32;
		pushConst.markerBit           = 1U << (uint(shadingType) % 32);

		pushConst.tileBufferCmdId     = asUAV(queue, ctx.tileCmdBuffer);
		pushConst.tileBufferCountId   = asUAV(queue, countBuffer);

		{
			uint2 dispatchDim = divideRoundingUp(marker.markerDim, uint2(16));
			auto computeShader = getContext().getShaderLibrary().getShader<VisibilityTilePrepareCS>();
			addComputePass2(
				queue,
				"VisibilityTilePrepare CS",
				getContext().computePipe(computeShader, "VisibilityTilePreparePipe"),
				pushConst,
				math::uvec3(dispatchDim.x, dispatchDim.y, 1));
		}


		pushConst.tileDrawCmdId = asUAV(queue, ctx.dispatchIndirectBuffer);
		pushConst.tileBufferCountId = asSRV(queue, countBuffer);

		{
			auto computeShader = getContext().getShaderLibrary().getShader<VisibilityTileCmdFillCS>();
			addComputePass2(
				queue,
				"TileIndirectBufferFill CS",
				getContext().computePipe(computeShader, "TileIndirectBufferFillPipe"),
				pushConst,
				math::uvec3(1, 1, 1));


		}

		asSRV(queue, ctx.dispatchIndirectBuffer);
		asSRV(queue, ctx.tileCmdBuffer);

		return ctx;
	}

}