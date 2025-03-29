#include <shader_compiler/shader.h>
#include <shader_compiler/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/lighting.h>
#include <shader/nanite_debug.hlsl>
#include <shader/accelerate_structure_visualize.hlsl>

namespace chord 
{
	static int32 sNaniteVisualizationConfig = -1;
	static AutoCVarRef cVarNaniteVisualizationConfig(
		"r.visualize.nanite",
		sNaniteVisualizationConfig,
		"**** Nanite visualize mode ****"
		"  -1. default state, do nothing."
		"   0. draw visible meshlet hash color."
		"   1. draw triangle id hash color."
		"   2. draw lod hash color."
		"   3. draw lod and meshle hash color."
		"   4. draw barycentrics."
	);

	static int32 sAccelerateStructureVisualizationConfig = -1;
	static AutoCVarRef cVarAccelerateStructureVisualizationConfig(
		"r.visualize.accelerateStructure",
		sAccelerateStructureVisualizationConfig,
		"**** Accelerate structure visualize mode ****"
		"  -1. default state, do nothing."
		"   0. draw visible meshlet hash color."
	);

	namespace graphics
	{
		PRIVATE_GLOBAL_SHADER(NaniteVisualizePS, "resource/shader/nanite_debug.hlsl", "mainPS", EShaderStage::Pixel);
		PRIVATE_GLOBAL_SHADER(AccelerateStructureVisualizeCS, "resource/shader/accelerate_structure_visualize.hlsl", "mainCS", EShaderStage::Compute);
	}

	void chord::visualizeAccelerateStructure(
		graphics::GraphicsQueue& queue,
		const AtmosphereLut& luts,
		const CascadeShadowContext& cascadeCtx,
		GBufferTextures& gbuffers,
		uint32 cameraViewId,
		graphics::helper::AccelKHRRef tlas)
	{
		using namespace graphics;

		if (sAccelerateStructureVisualizationConfig < 0)
		{
			return;
		}

		if (!getContext().isRaytraceSupport())
		{
			return;
		}

		AccelerationStructureVisualizePushConsts pushConst{};
		pushConst.cameraViewId = cameraViewId;
		pushConst.uav = asUAV(queue, gbuffers.color);
		pushConst.cascadeCount = cascadeCtx.depths.size();
		pushConst.shadowViewId = cascadeCtx.viewsSRV;
		pushConst.shadowDepthIds = cascadeCtx.cascadeShadowDepthIds;
		pushConst.linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
		pushConst.transmittanceId = asSRV(queue, luts.transmittance);
		pushConst.scatteringId = asSRV3DTexture(queue, luts.scatteringTexture);
		if (luts.optionalSingleMieScatteringTexture != nullptr)
		{
			pushConst.singleMieScatteringId = asSRV3DTexture(queue, luts.optionalSingleMieScatteringTexture);
		}

		uint2 dispatchParam = { (gbuffers.dimension + 7U) / 8U };

		auto computeShader = getContext().getShaderLibrary().getShader<AccelerateStructureVisualizeCS>();
		addComputePass(queue,
			"Visualize: AccelerateStructureCS",
			getContext().computePipe(computeShader, "Visualize: AccelerateStructure", {
				getContext().descriptorFactoryBegin()
				.accelerateStructure(0) // TLAS
				.buildNoInfoPush() }),
			{ dispatchParam, 1 },
			[&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
			{
				pipe->pushConst(cmd, pushConst);

				PushSetBuilder(queue, cmd)
					.addAccelerateStructure(tlas)
					.push(pipe, 1); // Push set 1.
			});
	}

	void chord::visualizeNanite(graphics::GraphicsQueue& queue, GBufferTextures& gbuffers, uint32 cameraViewId, graphics::PoolBufferGPUOnlyRef drawMeshletCmdBuffer, const VisibilityTileMarkerContext& marker)
	{
		using namespace graphics;

		if (sNaniteVisualizationConfig < 0)
		{
			return;
		}

		RenderTargets RTs{ };
		RTs.RTs[0] = RenderTargetRT(gbuffers.color, ERenderTargetLoadStoreOp::Nope_Store);

		NaniteDebugPushConsts pushConst{};
		pushConst.visibilityTexelSize = math::vec2(1.0f) / math::vec2(marker.visibilityDim);
		pushConst.visibilityTextureId = asSRV(queue, marker.visibilityTexture);
		pushConst.cameraViewId = cameraViewId;
		pushConst.drawedMeshletCmdId = asSRV(queue, drawMeshletCmdBuffer);
		pushConst.debugType = sNaniteVisualizationConfig;


		addFullScreenPass2<NaniteVisualizePS>(queue, "NaniteVisualize", RTs, pushConst);
	}

}