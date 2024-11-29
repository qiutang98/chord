#include <renderer/lighting.h>
#include <renderer/renderer.h>
#include <shader/gi.h>
#include <shader/gi_screen_probe_spawn.hlsl>
#include <shader/gi_screen_probe_trace.hlsl>
#include <shader/gi_screen_probe_project_sh.hlsl>
#include <shader/gi_screen_probe_interpolate.hlsl>
#include <random>

using namespace chord;
using namespace chord::graphics;

static uint32 sEnableGI = 1;
static AutoCVarRef cVarEnableGI(
	"r.gi",
	sEnableGI,
	"Enable gi or not."
);

static uint32 sEnableGIDebugOutput = 0;
static AutoCVarRef cVarEnableGIDebugOutput(
	"r.gi.debug",
	sEnableGIDebugOutput,
	"Enable gi debug output or not."
);

PRIVATE_GLOBAL_SHADER(GIScreenProbeSpawnCS, "resource/shader/gi_screen_probe_spawn.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIScreenProbeTraceCS, "resource/shader/gi_screen_probe_trace.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIScreenProbeProjectSHCS, "resource/shader/gi_screen_probe_project_sh.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIScreenProbeInterpolateCS, "resource/shader/gi_screen_probe_interpolate.hlsl", "mainCS", EShaderStage::Compute);

graphics::PoolTextureRef chord::giUpdate(
	graphics::CommandList& cmd, 
	graphics::GraphicsQueue& queue, 
	const AtmosphereLut& luts, 
	const CascadeShadowContext& cascadeCtx, 
	GBufferTextures& gbuffers, 
	uint32 cameraViewId, 
	graphics::helper::AccelKHRRef tlas, 
	ICamera* camera)
{
	if (!sEnableGI || !getContext().isRaytraceSupport())
	{
		return nullptr;
	}

	auto& rtPool = getContext().getTexturePool();
	auto& bufferPool = getContext().getBufferPool();

	uint2 halfDim = gbuffers.dimension / 2u;
	uint2 screenProbeDim = halfDim / 8u;

	// 
	auto newScreenProbeSpawnInfoBuffer = bufferPool.createGPUOnly("GI-ScreenProbe-SpawnInfo", sizeof(uint4) * screenProbeDim.x * screenProbeDim.y, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	// Pass #0: Spawn screen space probe. 
	{
		GIScreenProbeSpawnPushConsts pushConsts{};
		const uint2 dispatchDim = divideRoundingUp(screenProbeDim, uint2(8));

		pushConsts.probeDim          = screenProbeDim;
		pushConsts.cameraViewId      = cameraViewId;
		pushConsts.probeSpawnInfoUAV = asUAV(queue, newScreenProbeSpawnInfoBuffer);
		pushConsts.depthSRV          = asSRV(queue, gbuffers.depth_Half);
		pushConsts.normalRSId        = asSRV(queue, gbuffers.pixelRSNormal_Half);

		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeSpawnCS>();
		addComputePass2(queue,
			"GI: ScreenProbeSpawn",
			getContext().computePipe(computeShader, "GI: ScreenProbeSpawn"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	// Pass #1: Screen probe trace. 
	auto screenProbeTraceRadianceRT = rtPool.create("GI-ScreenProbe-TraceRadiance", halfDim.x, halfDim.y, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	{
		// Clear trace radiance RT. 
		{
			static const auto clearValue = VkClearColorValue{ .uint32 = { 0, 0, 0, 0} };
			static const auto range = helper::buildBasicImageSubresource();
			queue.clearImage(screenProbeTraceRadianceRT, &clearValue, 1, &range);
		}

		GIScreenProbeTracePushConsts pushConsts{};

		pushConsts.probeDim = screenProbeDim;
		pushConsts.gbufferDim = halfDim;

		pushConsts.cascadeCount = cascadeCtx.depths.size();
		pushConsts.shadowViewId = cascadeCtx.viewsSRV;
		pushConsts.shadowDepthIds = cascadeCtx.cascadeShadowDepthIds;
		pushConsts.transmittanceId = asSRV(queue, luts.transmittance);

		pushConsts.scatteringId = asSRV3DTexture(queue, luts.scatteringTexture);
		if (luts.optionalSingleMieScatteringTexture != nullptr)
		{
			pushConsts.singleMieScatteringId = asSRV3DTexture(queue, luts.optionalSingleMieScatteringTexture);
		}
		pushConsts.irradianceTextureId = asSRV(queue, luts.irradianceTexture);
		pushConsts.linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();

		pushConsts.minRayTraceDistance = 0.1f;
		pushConsts.maxRayTraceDistance = 800.0f;
		pushConsts.cameraViewId        = cameraViewId;
		pushConsts.probeSpawnInfoSRV   = asSRV(queue, newScreenProbeSpawnInfoBuffer);
		pushConsts.rayHitLODOffset     = 2.0f;
		pushConsts.radianceUAV      = asUAV(queue, screenProbeTraceRadianceRT);

		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeTraceCS>();
		addComputePass(queue,
			"GI: ScreenProbeTrace",
			getContext().computePipe(computeShader, "GI: ScreenProbeTrace", {
				getContext().descriptorFactoryBegin()
				.accelerateStructure(0) // TLAS
				.buildNoInfoPush() }),
			{ screenProbeDim.x, screenProbeDim.y, 1 },
			[&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
			{
				pipe->pushConst(cmd, pushConsts);

				PushSetBuilder(queue, cmd)
					.addAccelerateStructure(tlas)
					.push(pipe, 1); // Push set 1.
			});

		asSRV(queue, screenProbeTraceRadianceRT);
	}

	auto newScreenProbeSHBuffer = bufferPool.createGPUOnly("GI-ScreenProbe-SH", sizeof(float3) * 9 * screenProbeDim.x * screenProbeDim.y, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	queue.clearUAV(newScreenProbeSHBuffer);
	{
		GIScreenProbeProjectSHPushConsts pushConst{};
		pushConst.probeDim          = screenProbeDim;
		pushConst.cameraViewId      = cameraViewId;
		pushConst.probeSpawnInfoSRV = asSRV(queue, newScreenProbeSpawnInfoBuffer);
		pushConst.radianceSRV       = asSRV(queue, screenProbeTraceRadianceRT);
		pushConst.screenProbeSHUAV  = asUAV(queue, newScreenProbeSHBuffer);
		pushConst.gbufferDim        = halfDim;

		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeProjectSHCS>();
		addComputePass2(queue,
			"GI: ScreenProbeSH",
			getContext().computePipe(computeShader, "GI: ScreenProbeSH"),
			pushConst,
			{ screenProbeDim.x, screenProbeDim.y, 1 });
	}

	auto giInterpolateRT = rtPool.create("GI-ScreenProbe-Interpolate", halfDim.x, halfDim.y, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	{
		GIScreenProbeInterpolatePushConsts pushConsts{};
		pushConsts.probeDim = screenProbeDim;
		pushConsts.gbufferDim = halfDim;

		pushConsts.cameraViewId = cameraViewId;
		pushConsts.probeSpawnInfoSRV = asSRV(queue, newScreenProbeSpawnInfoBuffer);
		pushConsts.depthSRV = asSRV(queue, gbuffers.depth_Half);
		pushConsts.normalRSId = asSRV(queue, gbuffers.pixelRSNormal_Half);

		pushConsts.diffuseGIUAV = asUAV(queue, giInterpolateRT);
		pushConsts.screenProbeSHSRV = asSRV(queue, newScreenProbeSHBuffer);

		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));

		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeInterpolateCS>();
		addComputePass2(queue,
			"GI: Interpolate",
			getContext().computePipe(computeShader, "GI: Interpolate"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });

		
	}

	if (sEnableGIDebugOutput == 1)
	{
		return giInterpolateRT;
	}
	else if (sEnableGIDebugOutput == 2)
	{
		return screenProbeTraceRadianceRT;
	}

	return nullptr;
}