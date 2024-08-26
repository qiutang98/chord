#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/lighting.h>
#include <shader/lighting.hlsl>

namespace chord
{
	using namespace graphics;

	class LightingCS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);
		class SV_ShadingType : SHADER_VARIANT_ENUM("LIGHTING_TYPE", EShadingType);
		using Permutation = TShaderVariantVector<SV_ShadingType>;
	};
	IMPLEMENT_GLOBAL_SHADER(LightingCS, "resource/shader/lighting.hlsl", "mainCS", EShaderStage::Compute);

	void chord::lighting(GraphicsQueue& queue, GBufferTextures& gbuffers, uint32 cameraViewId, PoolBufferGPUOnlyRef drawMeshletCmdBuffer, const VisibilityTileMarkerContext& marker)
	{
		uint sceneColorUAV = asUAV(queue, gbuffers.color);
		uint2 dispatchDim = divideRoundingUp(marker.visibilityDim, uint2(8));

		for (uint i = 0; i < uint(EShadingType::MAX); i++)
		{
			if (i == kLightingType_None) { continue; }

			auto lightingTileCtx = prepareShadingTileParam(queue, EShadingType(i), marker);

			LightingPushConsts pushConst{};
			pushConst.visibilityTexelSize = math::vec2(1.0f) / math::vec2(marker.visibilityDim);
			pushConst.visibilityDim = marker.visibilityDim;
			pushConst.cameraViewId = cameraViewId;
			pushConst.tileBufferCmdId = asSRV(queue, lightingTileCtx.tileCmdBuffer);
			pushConst.visibilityId = asSRV(queue, marker.visibilityTexture);
			pushConst.sceneColorId = sceneColorUAV;
			pushConst.drawedMeshletCmdId = asSRV(queue, drawMeshletCmdBuffer);

			LightingCS::Permutation CSPermutation;
			CSPermutation.set<LightingCS::SV_ShadingType>(EShadingType(i));
			auto computeShader = getContext().getShaderLibrary().getShader<LightingCS>(CSPermutation);
			addIndirectComputePass2(
				queue,
				"LightingTile CS",
				getContext().computePipe(computeShader, "LightingTilePipe"),
				pushConst,
				lightingTileCtx.dispatchIndirectBuffer);
		}
	}
}