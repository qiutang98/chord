#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/lighting.h>
#include <shader/nanite_debug.hlsl>

using namespace chord;
using namespace chord::graphics;

static int32 sNaniteVisualizationConfig = -1;
static AutoCVarRef cVarInstanceCullingShaderDebugMode(
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


PRIVATE_GLOBAL_SHADER(NaniteVisualizePS, "resource/shader/nanite_debug.hlsl", "mainPS", EShaderStage::Pixel);

void chord::visualizeNanite(GraphicsQueue& queue, GBufferTextures& gbuffers, uint32 cameraViewId, PoolBufferGPUOnlyRef drawMeshletCmdBuffer, const VisibilityTileMarkerContext& marker)
{
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
