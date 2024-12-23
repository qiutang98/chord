#include <renderer/lighting.h>
#include <renderer/renderer.h>
#include <shader/gi.h>
#include <shader/gi_screen_probe_spawn.hlsl>
#include <shader/gi_screen_probe_trace.hlsl>
#include <shader/gi_screen_probe_project_sh.hlsl>
#include <shader/gi_screen_probe_interpolate.hlsl>
#include <shader/gi_world_probe_sh_inject.hlsl>
#include <shader/gi_world_probe_sh_update.hlsl>
#include <shader/gi_world_probe_sh_propagate.hlsl>
#include <shader/gi_screen_probe_sh_reproject.hlsl>
#include <shader/gi_history_reprojection.hlsl>
#include <shader/gi_spatial_filter_diffuse.hlsl>
#include <shader/gi_upsample.hlsl>
#include <shader/gi_specular_trace.hlsl>
#include <shader/gi_spatial_filter_specular.hlsl>
#include <shader/gi_spatial_specular_remove_fireflare.hlsl>
#include <shader/gi_rt_ao.hlsl>
#include <shader/gi_ssao.hlsl>

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

static float sGISkylightLeaking = 0.05f;
static AutoCVarRef cVarGISkylightLeaking(
	"r.gi.skylightleaking",
	sGISkylightLeaking,
	"Sky light leaking ratio when trace."
);

static uint32 sGIEnableSpatialFilter = 1;
static AutoCVarRef cVarGIEnableSpatialFilter(
	"r.gi.diffuse.spatial",
	sGIEnableSpatialFilter,
	"GI diffuse spatial filter pass enable or not."
);

static uint32 sGIEnableSpecularSpatialFilter = 1;
static AutoCVarRef cVarGISpecularEnableSpatialFilter(
	"r.gi.specular.spatial",
	sGIEnableSpecularSpatialFilter,
	"GI specular spatial filter pass enable or not." 
);

static uint32 sGIInterpretationJustUseWorldCache = 0; 
static AutoCVarRef cVarGIInterpretationJustUseWorldCache(
	"r.gi.interpolation.justUseWorldCache",
	sGIInterpretationJustUseWorldCache,
	"Just use world cache radiance when interpolate or not."
);

static uint32 sGIInterpretationDisableWorldCache = 0;
static AutoCVarRef cVarGIInterpretationDisableWorldCache(
	"r.gi.interpolation.disableWorldCache",
	sGIInterpretationDisableWorldCache,
	"Disable world cache radiance when interpolate or not."
);

static uint32 sGITraceSampleWorldCache = 1;
static AutoCVarRef cVarGITraceSampleWorldCache(
	"r.gi.trace.sampleWorldCache",
	sGITraceSampleWorldCache,
	"Sample world cache radiance when trace or not."
);

static uint32 sGIWorldCacheProbeDim = 32;
static AutoCVarRef cVarGIWorldCacheProbeDim(
	"r.gi.worldcache.probeDim",
	sGIWorldCacheProbeDim,
	"GI world probe cache dim, only 16, 24, 32, 48, 64 recommend used."
);

static float sGIWorldCacheProbeVoxelSize = 1.0f;
static AutoCVarRef cVarGIWorldCacheProbeVoxelSize(
	"r.gi.worldcache.voxelSize",
	sGIWorldCacheProbeVoxelSize,
	"GI world probe voxel size."
);

static uint32 sAOMethod = 1;
static AutoCVarRef cVarAOMethodGI(
	"r.gi.aoMethod",
	sAOMethod,
	"AO method when composite to GI."
	" 0: None."
	" 1: SSAO."
	" 2: RTAO."
);

static float sGIRTAORayLenght = 1.0f;
static AutoCVarRef cVarGIRTAORayLenght(
	"r.gi.rtao.raylenght",
	sGIRTAORayLenght,
	"RTAO ray lenght."
);

static float sGIRTAOPower = 0.25f;
static AutoCVarRef cVarRTAOPower(
	"r.gi.rtao.power",
	sGIRTAOPower,
	"RTAO ray power."
);

static float sGISSAO_ViewRadius = 0.2f;
static AutoCVarRef cVarGISSAO_ViewRadius(
	"r.gi.ssao.viewRadius",
	sGISSAO_ViewRadius,
	"SSAO view radius."
);

static float sGISSAO_Falloff = 0.1f;
static AutoCVarRef cVarGISSAO_Falloff(
	"r.gi.ssao.falloff",
	sGISSAO_Falloff,
	"SSAO falloff."
);

static uint32 sGISSAO_StepCount = 4;
static AutoCVarRef cVarGISSAO_StepCount(
	"r.gi.ssao.stepCount",
	sGISSAO_StepCount,
	"SSAO step count."
);

static uint32 sGISSAO_SliceCount = 2;
static AutoCVarRef cVarGISSAO_SliceCount(
	"r.gi.ssao.sliceCount",
	sGISSAO_SliceCount,
	"SSAO slice count."
);

PRIVATE_GLOBAL_SHADER(GIScreenProbeSpawnCS, "resource/shader/gi_screen_probe_spawn.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIScreenProbeInterpolateCS, "resource/shader/gi_screen_probe_interpolate.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIScreenProbeTraceCS, "resource/shader/gi_screen_probe_trace.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIScreenProbeProjectSHCS, "resource/shader/gi_screen_probe_project_sh.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIScreenProbeSHInjectCS, "resource/shader/gi_world_probe_sh_inject.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIWorldProbeSHUpdateCS, "resource/shader/gi_world_probe_sh_update.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIWorldProbeSHPropagateCS, "resource/shader/gi_world_probe_sh_propagate.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIScreenProbeSHReprojectCS, "resource/shader/gi_screen_probe_sh_reproject.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIHistoryReprojectCS, "resource/shader/gi_history_reprojection.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIDiffuseSpatialFilterCS, "resource/shader/gi_spatial_filter_diffuse.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GISpecularSpatialFilterCS, "resource/shader/gi_spatial_filter_specular.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GISpecularRemoveFireFlareFilterCS, "resource/shader/gi_spatial_specular_remove_fireflare.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GISpatialUpsampleCS, "resource/shader/gi_upsample.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GISpecularTraceCS, "resource/shader/gi_specular_trace.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIRTAOCS, "resource/shader/gi_rt_ao.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GISSAOCS, "resource/shader/gi_ssao.hlsl", "mainCS", EShaderStage::Compute);

void chord::giUpdate(
	graphics::CommandList& cmd,
	graphics::GraphicsQueue& queue,
	const AtmosphereLut& luts,
	const CascadeShadowContext& cascadeCtx,
	GIContext& giCtx,
	GBufferTextures& gbuffers,
	uint32 cameraViewId,
	graphics::helper::AccelKHRRef tlas,
	const DisocclusionPassResult& disocclusionCtx,
	ICamera* camera,
	graphics::PoolTextureRef hzb,
	const PerframeCameraView& perframe,
	graphics::PoolTextureRef depth_Half_LastFrame,
	graphics::PoolTextureRef pixelNormalRS_Half_LastFrame,
	bool bCameraCut,
	RendererTimerLambda timer)
{
	if (!sEnableGI || !getContext().isRaytraceSupport())
	{
		// Reset cache.
		giCtx = { };
		bCameraCut = true;

		return;
	}

	auto& rtPool = getContext().getTexturePool();
	auto& bufferPool = getContext().getBufferPool();

	// Allocate world probe resource. 
	uint32 worldProbeConfigBufferId;
	uint32 worldProbeCascadeCount;
	bool bHistoryInvalid = false;
	{
		static constexpr int   kCascadeCount = 8;
		const int3 kProbeDim = { sGIWorldCacheProbeDim, sGIWorldCacheProbeDim, sGIWorldCacheProbeDim };
		const float kVoxelSize = sGIWorldCacheProbeVoxelSize; // 1.0; 2.0; 4.0; 8.0; 16.0; 32.0; 64.0; 128.0;
		{
			float voxelSize = kVoxelSize;
			for (int i = 0; i < kCascadeCount; i++)
			{
				if (giCtx.volumes.size() <= i)
				{
					giCtx.volumes.push_back({});
					giCtx.volumes[i].scrollAnchor = camera->getPosition();

					// 
					bHistoryInvalid = true;
				}

				auto& resource = giCtx.volumes[i];
				bHistoryInvalid
					|= (resource.probeDim != kProbeDim)
					|| (resource.probeSpacing.x != voxelSize)
					|| (resource.probeIrradianceBuffer == nullptr);

				voxelSize *= 2.0f;
			}
		}

		//
		{
			float voxelSize = kVoxelSize;
			for (int i = 0; i < kCascadeCount; i++)
			{
				auto& resource = giCtx.volumes[i];
				if (bHistoryInvalid)
				{
					resource.probeDim = kProbeDim;
					resource.probeSpacing = { voxelSize, voxelSize, voxelSize };
				}
				else
				{
					check(resource.probeSpacing.x == voxelSize);
					check(resource.probeDim == kProbeDim);
				}

				if (bHistoryInvalid)
				{
					resource.probeIrradianceBuffer = bufferPool.createGPUOnly("GI-WorldProbe-SH-Irradiance",
						sizeof(SH3_gi_pack) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
						VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
					queue.clearUAV(resource.probeIrradianceBuffer, 0U);
				}

				// 
				voxelSize *= 2.0f;
			}
		}

		std::vector<GIWorldProbeVolumeConfig> worldProbeConfigs{ };
		worldProbeConfigs.resize(kCascadeCount);
		{
			for (int i = 0; i < kCascadeCount; i++)
			{
				auto& resource = giCtx.volumes[i];

				worldProbeConfigs[i].probeDim = resource.probeDim;
				worldProbeConfigs[i].sh_UAV = asUAV(queue, resource.probeIrradianceBuffer);

				worldProbeConfigs[i].probeSpacing = resource.probeSpacing;
				worldProbeConfigs[i].sh_SRV = asSRV(queue, resource.probeIrradianceBuffer);

				worldProbeConfigs[i].bResetAll = bHistoryInvalid;

				// Scrolling.
				if (bHistoryInvalid)
				{
					worldProbeConfigs[i].currentScrollOffset = { 0, 0, 0 };
					worldProbeConfigs[i].scrollOffset = { 0, 0, 0 };
					worldProbeConfigs[i].probeCenterRS = { 0.0f, 0.0f, 0.0f };
				}
				else
				{
					double3 translation = resource.scrollAnchor - camera->getPosition();

					//
					int3 scroll =
					{
						absFloor(translation.x / double(resource.probeSpacing.x)),
						absFloor(translation.y / double(resource.probeSpacing.y)),
						absFloor(translation.z / double(resource.probeSpacing.z)),
					};

					worldProbeConfigs[i].currentScrollOffset = scroll;
					resource.scrollOffset += scroll;

					// Update anchor.
					resource.scrollAnchor -= double3(resource.probeSpacing * float3(scroll));

					// Update scroll offset.
					worldProbeConfigs[i].scrollOffset = resource.scrollOffset % int3(resource.probeDim);

					// 
					double3 newTranslation = resource.scrollAnchor - camera->getPosition();
					worldProbeConfigs[i].probeCenterRS = float3(newTranslation);
				}
			}
		}

		worldProbeConfigBufferId = uploadBufferToGPU(cmd, "GIWorldProbeVolumeConfigs", worldProbeConfigs.data(), worldProbeConfigs.size()).second;
		worldProbeCascadeCount = kCascadeCount;

		// SH Update. 
		for (int i = kCascadeCount - 1; i >= 0; i--)
		{
			auto& resource = giCtx.volumes[i];


			auto tempProbeIrradianceBuffer = bufferPool.createGPUOnly("GI-WorldProbe-SH-Irradiance-Temp",
				sizeof(SH3_gi_pack) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

			const uint dispatchDim = divideRoundingUp(kProbeDim.x * kProbeDim.y * kProbeDim.z, 64);

			{
				asSRV(queue, resource.probeIrradianceBuffer);

				GIWorldProbeSHUpdatePushConsts pushConst{};
				pushConst.cameraViewId = cameraViewId;
				pushConst.clipmapConfigBufferId = worldProbeConfigBufferId;
				pushConst.clipmapLevel = i;
				pushConst.sh_uav = asUAV(queue, tempProbeIrradianceBuffer);
				pushConst.bLastCascade = i == kCascadeCount - 1;

				auto computeShader = getContext().getShaderLibrary().getShader<GIWorldProbeSHUpdateCS>();
				addComputePass2(queue,
					"GI: WorldProbeSHUpdate",
					getContext().computePipe(computeShader, "GI: WorldProbeSHUpdate"),
					pushConst,
					{ dispatchDim, 1, 1 });
			}
			{
				GIWorldProbeSHPropagatePushConsts pushConst{};
				pushConst.cameraViewId = cameraViewId;
				pushConst.clipmapConfigBufferId = worldProbeConfigBufferId;
				pushConst.clipmapLevel = i;
				pushConst.sh_uav = asUAV(queue, resource.probeIrradianceBuffer);
				pushConst.sh_srv = asSRV(queue, tempProbeIrradianceBuffer);

				pushConst.energyLose = 0.90f;

				auto computeShader = getContext().getShaderLibrary().getShader<GIWorldProbeSHPropagateCS>();
				addComputePass2(queue,
					"GI: WorldProbeSHPropagate",
					getContext().computePipe(computeShader, "GI: WorldProbeSHPropagate"),
					pushConst,
					{ dispatchDim, 1, 1 });

				asSRV(queue, resource.probeIrradianceBuffer);
			}

		}
	}


	if (timer)
	{
		timer("GI: World Radiance Cache Update", queue);
	}


	uint2 halfDim = gbuffers.dimension / 2u;
	uint2 screenProbeDim = halfDim / 8u; // divideRoundingUp(halfDim, uint2(8));

	// 
	auto newScreenProbeSpawnInfoBuffer = bufferPool.createGPUOnly("GI-ScreenProbe-SpawnInfo", sizeof(uint3) * screenProbeDim.x * screenProbeDim.y, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	// Pass #0: Spawn screen space probe. 
	{
		GIScreenProbeSpawnPushConsts pushConsts{};
		const uint2 dispatchDim = divideRoundingUp(screenProbeDim, uint2(8));

		pushConsts.probeDim = screenProbeDim;
		pushConsts.cameraViewId = cameraViewId;
		pushConsts.probeSpawnInfoUAV = asUAV(queue, newScreenProbeSpawnInfoBuffer);
		pushConsts.depthSRV = asSRV(queue, gbuffers.depth_Half);
		pushConsts.normalRSId = asSRV(queue, gbuffers.pixelRSNormal_Half);
		pushConsts.gbufferDim = halfDim;

		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeSpawnCS>();
		addComputePass(queue,
			"GI: ScreenProbeSpawn",
			getContext().computePipe(computeShader, "GI: ScreenProbeSpawn", {
				getContext().descriptorFactoryBegin()
				.accelerateStructure(0) // TLAS
				.buildNoInfoPush() }),
			{ dispatchDim.x, dispatchDim.y, 1 },
			[&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
			{
				pipe->pushConst(cmd, pushConsts);

				PushSetBuilder(queue, cmd)
					.addAccelerateStructure(tlas)
					.push(pipe, 1); // Push set 1.
			});
	}

	auto probeReprojectStatRadianceRT = rtPool.create(
		"GI-ScreenProbe-ReprojectStat",
		screenProbeDim.x,
		screenProbeDim.y, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	auto probeReprojectTraceRadianceRT = rtPool.create(
		"GI-ScreenProbe-ReprojectTrace",
		halfDim.x, halfDim.y,
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	if (giCtx.screenProbeSampleRT == nullptr)
	{
		giCtx.screenProbeSampleRT = rtPool.create(
			"GI-ScreenProbe-Sample",
			screenProbeDim.x, screenProbeDim.y,
			VK_FORMAT_R8_UINT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

		static const auto clearValue = VkClearColorValue{ .uint32 = { 0, 0, 0, 0} };
		static const auto range = helper::buildBasicImageSubresource();
		queue.clearImage(giCtx.screenProbeSampleRT, &clearValue, 1, &range);
	}

	if (giCtx.screenProbeSpawnInfoBuffer != nullptr && giCtx.screenProbeTraceRadiance != nullptr) // History valid. 
	{
		GIScreenProbeSHReprojectPushConst pushConst{};
		pushConst.probeDim = screenProbeDim;
		pushConst.gbufferDim = halfDim;
		pushConst.cameraViewId = cameraViewId;
		pushConst.motionVectorId = asSRV(queue, gbuffers.motionVector_Half);

		pushConst.probeSpawnInfoSRV = asSRV(queue, newScreenProbeSpawnInfoBuffer);
		pushConst.historyProbeSpawnInfoSRV = asSRV(queue, giCtx.screenProbeSpawnInfoBuffer);

		pushConst.reprojectionStatUAV = asUAV(queue, probeReprojectStatRadianceRT);

		pushConst.historyProbeTraceRadianceSRV = asSRV(queue, giCtx.screenProbeTraceRadiance);
		pushConst.reprojectionRadianceUAV = asUAV(queue, probeReprojectTraceRadianceRT);

		pushConst.screenProbeSampleUAV = asUAV(queue, giCtx.screenProbeSampleRT);
		pushConst.bResetAll = bHistoryInvalid;
		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeSHReprojectCS>();
		addComputePass2(queue,
			"GI: ScreenProbeSHReproject",
			getContext().computePipe(computeShader, "GI: ScreenProbeSHReproject"),
			pushConst,
			{ screenProbeDim.x, screenProbeDim.y, 1 });
	}

	if (timer)
	{
		timer("GI: Screen Probe Spawn & Reproject", queue);
	}

	// Pass #1: Screen probe trace. 
	auto probeTraceRadianceRT = rtPool.create("GI-ScreenProbe-TraceRadiance", halfDim.x, halfDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	auto newScreenProbeSHBuffer = bufferPool.createGPUOnly("GI-ScreenProbe-SH", sizeof(SH3_gi_pack) * screenProbeDim.x * screenProbeDim.y, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
	{
		{
			GIProbeTracePushConsts tracePushConsts{};

			tracePushConsts.probeDim = screenProbeDim;
			tracePushConsts.gbufferDim = halfDim;
			tracePushConsts.cascadeCount = cascadeCtx.depths.size();
			tracePushConsts.shadowViewId = cascadeCtx.viewsSRV;
			tracePushConsts.shadowDepthIds = cascadeCtx.cascadeShadowDepthIds;
			tracePushConsts.transmittanceId = asSRV(queue, luts.transmittance);
			tracePushConsts.scatteringId = asSRV3DTexture(queue, luts.scatteringTexture);
			if (luts.optionalSingleMieScatteringTexture != nullptr)
			{
				tracePushConsts.singleMieScatteringId = asSRV3DTexture(queue, luts.optionalSingleMieScatteringTexture);
			}
			tracePushConsts.irradianceTextureId = asSRV(queue, luts.irradianceTexture);
			tracePushConsts.linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
			tracePushConsts.rayMissDistance = 128.0f;
			tracePushConsts.maxRayTraceDistance = 800.0f;
			tracePushConsts.cameraViewId = cameraViewId;
			tracePushConsts.rayHitLODOffset = 2.0f;

			for (int i = 0; i < worldProbeCascadeCount; i++)
			{
				asSRV(queue, giCtx.volumes[i].probeIrradianceBuffer);
			}
			tracePushConsts.clipmapConfigBufferId = worldProbeConfigBufferId;
			tracePushConsts.clipmapCount = worldProbeCascadeCount;
			tracePushConsts.bHistoryValid = !bHistoryInvalid;
			tracePushConsts.probeSpawnInfoSRV = asSRV(queue, newScreenProbeSpawnInfoBuffer);
			tracePushConsts.radianceUAV = asUAV(queue, probeTraceRadianceRT);
			tracePushConsts.skyLightLeaking = sGISkylightLeaking;
			tracePushConsts.bSampleWorldCache = (sGITraceSampleWorldCache != 0);
			tracePushConsts.historyTraceSRV = asSRV(queue, probeReprojectTraceRadianceRT);
			tracePushConsts.screenProbeSampleSRV = asSRV(queue, giCtx.screenProbeSampleRT);
			tracePushConsts.statSRV = bHistoryInvalid ? kUnvalidIdUint32 : asSRV(queue, probeReprojectStatRadianceRT);
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
					pipe->pushConst(cmd, tracePushConsts);

					PushSetBuilder(queue, cmd)
						.addAccelerateStructure(tlas)
						.push(pipe, 1); // Push set 1.
				});
		}
	}

	if (timer)
	{
		timer("GI: Screen Probe RT trace", queue);
	}

	// Screen SH projection. 
	{
		GIScreenProbeProjectSHPushConsts pushConst{};
		pushConst.probeDim = screenProbeDim;
		pushConst.cameraViewId = cameraViewId;
		pushConst.probeSpawnInfoSRV = asSRV(queue, newScreenProbeSpawnInfoBuffer);
		pushConst.radianceSRV = asSRV(queue, probeTraceRadianceRT);
		pushConst.shUAV = asUAV(queue, newScreenProbeSHBuffer);
		pushConst.gbufferDim = halfDim;
		pushConst.statSRV = bHistoryInvalid ? kUnvalidIdUint32 : asSRV(queue, probeReprojectStatRadianceRT);
		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeProjectSHCS>();
		addComputePass2(queue,
			"GI: ScreenProbeSH",
			getContext().computePipe(computeShader, "GI: ScreenProbeSH"),
			pushConst,
			{ screenProbeDim.x, screenProbeDim.y, 1 });
	}

	if (timer)
	{
		timer("GI: Screen Probe SH projection", queue);
	}

	{
		// Screen trace. 
		{



			// World probe inject. 
			{
				GIWorldProbeSHInjectPushConsts pushConsts{};
				pushConsts.probeDim = screenProbeDim;
				pushConsts.gbufferDim = halfDim;
				pushConsts.cameraViewId = cameraViewId;
				pushConsts.probeSpawnInfoSRV = asSRV(queue, newScreenProbeSpawnInfoBuffer);
				pushConsts.screenProbeSHSRV = asSRV(queue, newScreenProbeSHBuffer);
				pushConsts.clipmapConfigBufferId = worldProbeConfigBufferId;
				pushConsts.clipmapCount = worldProbeCascadeCount;

				for (int i = 0; i < worldProbeCascadeCount; i++)
				{
					asUAV(queue, giCtx.volumes[i].probeIrradianceBuffer);
				}

				check(worldProbeCascadeCount == 2 || worldProbeCascadeCount == 4 || worldProbeCascadeCount == 8);

				// One workgroup can handle probe count (included all cascade).
				uint worldGroupHandleProbe = 64 / worldProbeCascadeCount;
				const uint dispatchDim = divideRoundingUp(screenProbeDim.x * screenProbeDim.y, worldGroupHandleProbe);

				auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeSHInjectCS>();
				addComputePass2(queue,
					"GI: WorldProbeInject",
					getContext().computePipe(computeShader, "GI: WorldProbeInject"),
					pushConsts,
					{ dispatchDim, 1, 1 });

				for (int i = 0; i < worldProbeCascadeCount; i++)
				{
					asSRV(queue, giCtx.volumes[i].probeIrradianceBuffer);
				}
			}
		}
	}

	if (timer)
	{
		timer("GI: Screen Probe Inject World", queue);
	}

	static constexpr float kSpecularRayMissDistance = 1e5f; // Large enough for ray miss sky mark. 
	graphics::PoolTextureRef specularTraceRadianceRT = nullptr;
	{
		specularTraceRadianceRT = rtPool.create("GI-Specular-TraceRadiance", halfDim.x, halfDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		GISpecularTracePushConsts tracePushConsts{};

		tracePushConsts.gbufferDim = halfDim;
		tracePushConsts.cascadeCount = cascadeCtx.depths.size();
		tracePushConsts.shadowViewId = cascadeCtx.viewsSRV;
		tracePushConsts.shadowDepthIds = cascadeCtx.cascadeShadowDepthIds;
		tracePushConsts.transmittanceId = asSRV(queue, luts.transmittance);
		tracePushConsts.scatteringId = asSRV3DTexture(queue, luts.scatteringTexture);
		if (luts.optionalSingleMieScatteringTexture != nullptr)
		{
			tracePushConsts.singleMieScatteringId = asSRV3DTexture(queue, luts.optionalSingleMieScatteringTexture);
		}
		tracePushConsts.irradianceTextureId = asSRV(queue, luts.irradianceTexture);
		tracePushConsts.linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
		tracePushConsts.rayMissDistance = kSpecularRayMissDistance;
		tracePushConsts.maxRayTraceDistance = 800.0f;
		tracePushConsts.cameraViewId = cameraViewId;
		tracePushConsts.rayHitLODOffset = 2.0f;

		for (int i = 0; i < worldProbeCascadeCount; i++)
		{
			asSRV(queue, giCtx.volumes[i].probeIrradianceBuffer);
		}
		tracePushConsts.clipmapConfigBufferId = worldProbeConfigBufferId;
		tracePushConsts.clipmapCount = worldProbeCascadeCount;
		tracePushConsts.bHistoryValid = !bHistoryInvalid;
		tracePushConsts.skyLightLeaking = sGISkylightLeaking;
		tracePushConsts.bSampleWorldCache = (sGITraceSampleWorldCache != 0);
		tracePushConsts.depthId = asSRV(queue, gbuffers.depth_Half);
		tracePushConsts.normalRSId = asSRV(queue, gbuffers.pixelRSNormal_Half);
		tracePushConsts.roughnessId = asSRV(queue, gbuffers.roughness_Half);
		tracePushConsts.UAV = asUAV(queue, specularTraceRadianceRT);
		tracePushConsts.disocclusionMask = disocclusionCtx.disocclusionMask != nullptr ? asSRV(queue, disocclusionCtx.disocclusionMask) : kUnvalidIdUint32; //

		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));
		auto computeShader = getContext().getShaderLibrary().getShader<GISpecularTraceCS>();
		addComputePass(queue,
			"GI: SpecularTraceCS",
			getContext().computePipe(computeShader, "GI: SpecularTrace", {
				getContext().descriptorFactoryBegin()
				.accelerateStructure(0) // TLAS
				.buildNoInfoPush() }),
			{ dispatchDim.x, dispatchDim.y, 1 },
			[&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
			{
				pipe->pushConst(cmd, tracePushConsts);

				PushSetBuilder(queue, cmd)
					.addAccelerateStructure(tlas)
					.push(pipe, 1); // Push set 1.
			});
	}

	if (timer)
	{
		timer("GI: Specular Trace", queue);
	}

	graphics::PoolTextureRef probeReprojectStatSpecularRT = nullptr;
	graphics::PoolTextureRef reprojectGIRT = nullptr;
	graphics::PoolTextureRef reprojectSpecularRT = nullptr;

	if (giCtx.historyDiffuseRT != nullptr && disocclusionCtx.disocclusionMask != nullptr &&
		depth_Half_LastFrame != nullptr && pixelNormalRS_Half_LastFrame != nullptr)
	{
		reprojectGIRT = rtPool.create("GI-Reproject", halfDim.x, halfDim.y,
			VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		reprojectSpecularRT = rtPool.create("GI-Reproject-Specular", halfDim.x, halfDim.y,
			VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));


		probeReprojectStatSpecularRT = rtPool.create(
			"GI-Specular-ReprojectStat",
			screenProbeDim.x,
			screenProbeDim.y, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		GIHistoryReprojectPushConsts pushConst{};

		pushConst.gbufferDim = halfDim;
		pushConst.cameraViewId = cameraViewId;
		pushConst.motionVectorId = asSRV(queue, gbuffers.motionVector_Half);
		pushConst.reprojectGIUAV = asUAV(queue, reprojectGIRT);
		pushConst.depthSRV = asSRV(queue, gbuffers.depth_Half);
		pushConst.disoccludedMaskSRV = asSRV(queue, disocclusionCtx.disocclusionMask);
		pushConst.historyGISRV = asSRV(queue, giCtx.historyDiffuseRT);
		pushConst.historySpecularSRV = asSRV(queue, giCtx.historySpecularRT);
		pushConst.reprojectSpecularUAV = asUAV(queue, reprojectSpecularRT);
		pushConst.specularStatUAV = asUAV(queue, probeReprojectStatSpecularRT);
		pushConst.specularIntersectSRV = asSRV(queue, specularTraceRadianceRT);
		pushConst.normalRSId_LastFrame = asSRV(queue, pixelNormalRS_Half_LastFrame);
		pushConst.normalRSId = asSRV(queue, gbuffers.vertexRSNormal_Half);
		pushConst.depthTextureId_LastFrame = asSRV(queue, depth_Half_LastFrame);

		auto computeShader = getContext().getShaderLibrary().getShader<GIHistoryReprojectCS>();
		addComputePass2(queue,
			"GI: ReprojectHistory",
			getContext().computePipe(computeShader, "GI: ReprojectHistory"),
			pushConst,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	if (timer)
	{
		timer("GI: Reproject History", queue);
	}


	// Specular post trace filter.
	graphics::PoolTextureRef specularTraceRadiancePostFilterRT = rtPool.create("GI-Specular-TraceRadiance-PostFilter", halfDim.x, halfDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	{
		GISpecularRemoveFireFlareFilterPushConsts pushConst{};

		pushConst.gbufferDim = halfDim;
		pushConst.cameraViewId = cameraViewId;
		pushConst.depthSRV = asSRV(queue, gbuffers.depth_Half);
		pushConst.normalRS = asSRV(queue, gbuffers.pixelRSNormal_Half);
		pushConst.roughnessSRV = asSRV(queue, gbuffers.roughness_Half);
		pushConst.statSRV = probeReprojectStatSpecularRT != nullptr ? asSRV(queue, probeReprojectStatSpecularRT) : kUnvalidIdUint32; // 

		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));

		pushConst.SRV = asSRV(queue, specularTraceRadianceRT);
		pushConst.UAV = asUAV(queue, specularTraceRadiancePostFilterRT);

		auto computeShader = getContext().getShaderLibrary().getShader<GISpecularRemoveFireFlareFilterCS>();
		addComputePass2(queue,
			"GI: SpecularFilter-Post",
			getContext().computePipe(computeShader, "GI: SpecularFilter-Post"),
			pushConst,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	if (timer)
	{
		timer("GI: Specular Remove Fireflare", queue);
	}

	PoolTextureRef AORT = nullptr;
	if (sAOMethod == 1)
	{
		AORT = rtPool.create("GI-SSAO-BentNormal", halfDim.x, halfDim.y, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		{
			GISSAOPushConsts pushConsts{};
			pushConsts.workDim = halfDim;
			pushConsts.cameraViewId = cameraViewId;
			pushConsts.hzbSRV = asSRV(queue, hzb);

			pushConsts.depthSRV = asSRV(queue, gbuffers.depth_Half);
			pushConsts.normalSRV = asSRV(queue, gbuffers.vertexRSNormal_Half);

			const float viewRadius = sGISSAO_ViewRadius; // 0.2f
			const float falloff = sGISSAO_Falloff; // 0.1f

			pushConsts.maxPixelScreenRadius = 128.0f; // TOO large radius will cause texture cache miss.
			pushConsts.stepCount = sGISSAO_StepCount;
			pushConsts.sliceCount = sGISSAO_SliceCount;

			float falloffRange = viewRadius * falloff;
			float falloffFrom = viewRadius * (1.0f - falloff);

			pushConsts.uvRadius = viewRadius * 0.5f * math::max(perframe.viewToClip[0][0], perframe.viewToClip[1][1]);
			pushConsts.falloff_mul = -1.0f / falloffRange;
			pushConsts.falloff_add = falloffFrom / falloffRange + 1.0f;

			pushConsts.UAV = asUAV(queue, AORT);

			const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));

			auto computeShader = getContext().getShaderLibrary().getShader<GISSAOCS>();
			addComputePass2(queue,
				"GI: SSAO",
				getContext().computePipe(computeShader, "GI: SSAO"),
				pushConsts,
				{ dispatchDim.x, dispatchDim.y, 1 });
		}
		if (timer)
		{
			timer("GI: SSAO", queue);
		}
	}
	else if (sAOMethod == 2)
	{
		AORT = rtPool.create("GI-RTAO", halfDim.x, halfDim.y, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		{
			GIRTAOPushConsts pushConst{};
			pushConst.gbufferDim = halfDim;
			pushConst.cameraViewId = cameraViewId;

			pushConst.depthSRV = asSRV(queue, gbuffers.depth_Half);
			pushConst.normalRSId = asSRV(queue, gbuffers.vertexRSNormal_Half);

			pushConst.rayLength = sGIRTAORayLenght;
			pushConst.power = sGIRTAOPower;
			pushConst.rtAO_UAV = asUAV(queue, AORT);

			const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8, 4));

			auto computeShader = getContext().getShaderLibrary().getShader<GIRTAOCS>();
			addComputePass(queue,
				"GI: RTAO",
				getContext().computePipe(computeShader, "GI: RTAO", {
					getContext().descriptorFactoryBegin()
					.accelerateStructure(0) // TLAS
					.buildNoInfoPush() }),
				{ dispatchDim.x, dispatchDim.y, 1 },
				[&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
				{
					pipe->pushConst(cmd, pushConst);

					PushSetBuilder(queue, cmd)
						.addAccelerateStructure(tlas)
						.push(pipe, 1); // Push set 1.
				});
		}
		if (timer)
		{
			timer("GI: RTAO", queue);
		}
	}



	auto giInterpolateRT = rtPool.create("GI-ScreenProbe-Interpolate", halfDim.x, halfDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	auto specularInterpolateRT = rtPool.create("GI-Specular-Interpolate", halfDim.x, halfDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	auto filteredRadiusRT = rtPool.create("GI-Spatial-FilterRadius", halfDim.x, halfDim.y, VK_FORMAT_R8G8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	{
		GIScreenProbeInterpolatePushConsts pushConsts{};
		pushConsts.probeDim = screenProbeDim;
		pushConsts.gbufferDim = halfDim;

		pushConsts.cameraViewId = cameraViewId;
		pushConsts.probeSpawnInfoSRV = asSRV(queue, newScreenProbeSpawnInfoBuffer);
		pushConsts.depthSRV = asSRV(queue, gbuffers.depth_Half);
		pushConsts.normalRSId = asSRV(queue, gbuffers.pixelRSNormal_Half);
		pushConsts.motionVectorId = asSRV(queue, gbuffers.motionVector_Half);
		pushConsts.diffuseGIUAV = asUAV(queue, giInterpolateRT);
		pushConsts.screenProbeSHSRV = asSRV(queue, newScreenProbeSHBuffer);
		pushConsts.clipmapConfigBufferId = worldProbeConfigBufferId;
		pushConsts.clipmapCount = worldProbeCascadeCount;
		pushConsts.screenProbeSampleSRV = asSRV(queue, giCtx.screenProbeSampleRT);
		pushConsts.reprojectSRV = reprojectGIRT == nullptr ? kUnvalidIdUint32 : asSRV(queue, reprojectGIRT); // 
		pushConsts.radiusUAV = asUAV(queue, filteredRadiusRT);

		pushConsts.reprojectSpecularSRV = reprojectSpecularRT == nullptr ? kUnvalidIdUint32 : asSRV(queue, reprojectSpecularRT); // 
		pushConsts.specularUAV = asUAV(queue, specularInterpolateRT);
		pushConsts.rouhnessSRV = asSRV(queue, gbuffers.roughness_Half);
		pushConsts.specularTraceSRV = asSRV(queue, specularTraceRadiancePostFilterRT);
		pushConsts.specularStatUAV = probeReprojectStatSpecularRT != nullptr ? asUAV(queue, probeReprojectStatSpecularRT) : kUnvalidIdUint32; // 
		pushConsts.bJustUseWorldCache = (sGIInterpretationJustUseWorldCache != 0);
		pushConsts.bDisableWorldCache = (sGIInterpretationDisableWorldCache != 0);
		pushConsts.rtAOSRV = AORT != nullptr ? asSRV(queue, AORT) : getContext().getWhiteTextureSRV();
		pushConsts.AOMethod = sAOMethod;
		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));

		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeInterpolateCS>();
		addComputePass2(queue,
			"GI: Interpolate",
			getContext().computePipe(computeShader, "GI: Interpolate"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	if (timer)
	{
		timer("GI: Interpolate", queue);
	}

	if (sGIEnableSpatialFilter != 0)
	{
		GISpatialFilterPushConsts pushConsts{ };
		pushConsts.gbufferDim = halfDim;
		pushConsts.cameraViewId = cameraViewId;
		pushConsts.originSRV = asSRV(queue, filteredRadiusRT);
		pushConsts.depthSRV = asSRV(queue, gbuffers.depth_Half);
		pushConsts.normalRS = asSRV(queue, gbuffers.pixelRSNormal_Half);

		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));
		auto computeShader = getContext().getShaderLibrary().getShader<GIDiffuseSpatialFilterCS>();

		auto filteredGIRT_Temp = rtPool.create("GI-ScreenProbe-Interpolate", halfDim.x, halfDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		pushConsts.SRV = asSRV(queue, giInterpolateRT);
		pushConsts.UAV = asUAV(queue, filteredGIRT_Temp);
		pushConsts.direction = int2(1, 0);
		addComputePass2(queue,
			"GI: SpatialFilter-X",
			getContext().computePipe(computeShader, "GI: SpatialFilter"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });

		// 
		pushConsts.SRV = asSRV(queue, filteredGIRT_Temp);
		pushConsts.UAV = asUAV(queue, giInterpolateRT);
		pushConsts.direction = int2(0, 1);

		addComputePass2(queue,
			"GI: SpatialFilter-Y",
			getContext().computePipe(computeShader, "GI: SpatialFilter"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	if (timer)
	{
		timer("GI: Diffuse Spatial", queue);
	}

	if (sGIEnableSpecularSpatialFilter != 0)
	{
		GISpecularSpatialFilterPushConsts pushConst{};

		pushConst.gbufferDim = halfDim;
		pushConst.cameraViewId = cameraViewId;
		pushConst.originSRV = asSRV(queue, filteredRadiusRT);
		pushConst.depthSRV = asSRV(queue, gbuffers.depth_Half);
		pushConst.normalRS = asSRV(queue, gbuffers.pixelRSNormal_Half);
		pushConst.roughnessSRV = asSRV(queue, gbuffers.roughness_Half);
		pushConst.statSRV = probeReprojectStatSpecularRT != nullptr ? asSRV(queue, probeReprojectStatSpecularRT) : kUnvalidIdUint32; //

		auto filteredGIRT_Temp = rtPool.create("GI-ScreenProbe-Interpolate", halfDim.x, halfDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));
		auto computeShader = getContext().getShaderLibrary().getShader<GISpecularSpatialFilterCS>();

		pushConst.SRV = asSRV(queue, specularInterpolateRT);
		pushConst.UAV = asUAV(queue, filteredGIRT_Temp);
		pushConst.direction = int2(1, 0);
		addComputePass2(queue,
			"GI: SpecularFilter - X",
			getContext().computePipe(computeShader, "GI: SpecularFilter - X"),
			pushConst,
			{ dispatchDim.x, dispatchDim.y, 1 });

		pushConst.SRV = asSRV(queue, filteredGIRT_Temp);
		pushConst.UAV = asUAV(queue, specularInterpolateRT);
		pushConst.direction = int2(0, 1);
		addComputePass2(queue,
			"GI: SpecularFilter - Y",
			getContext().computePipe(computeShader, "GI: SpecularFilter - Y"),
			pushConst,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	if (timer)
	{
		timer("GI: Specular Spatial", queue);
	}

	// auto fullResDiffuseGI = rtPool.create("GI-FullRes-DiffuseGI", gbuffers.dimension.x, gbuffers.dimension.y, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	{
		GIUpsamplePushConsts pushConsts{};
		pushConsts.fullDim = gbuffers.dimension;
		pushConsts.lowDim = halfDim;
		pushConsts.cameraViewId = cameraViewId;
		pushConsts.full_depthSRV = asSRV(queue, gbuffers.depthStencil, helper::buildDepthImageSubresource());
		pushConsts.full_normalSRV = asSRV(queue, gbuffers.pixelRSNormal);

		pushConsts.low_depthSRV = asSRV(queue, gbuffers.depth_Half);
		pushConsts.low_normalSRV = asSRV(queue, gbuffers.pixelRSNormal_Half);

		pushConsts.SRV = asSRV(queue, giInterpolateRT);
		pushConsts.SRV_1 = asSRV(queue, specularInterpolateRT);
		pushConsts.UAV = asUAV(queue, gbuffers.color);
		pushConsts.baseColorId = asSRV(queue, gbuffers.baseColor);
		pushConsts.aoRoughnessMetallicId = asSRV(queue, gbuffers.aoRoughnessMetallic);

		const uint2 dispatchDim = divideRoundingUp(gbuffers.dimension, uint2(8));
		auto computeShader = getContext().getShaderLibrary().getShader<GISpatialUpsampleCS>();
		addComputePass2(queue,
			"GI: SpatialUpsample",
			getContext().computePipe(computeShader, "GI: SpatialUpsample"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	if (timer)
	{
		timer("GI: Upsample & Composite", queue);
	}

	// Store history. 
	giCtx.screenProbeSpawnInfoBuffer = newScreenProbeSpawnInfoBuffer;
	giCtx.screenProbeTraceRadiance = probeTraceRadianceRT;
	giCtx.historyDiffuseRT = giInterpolateRT;
	giCtx.historySpecularRT = specularInterpolateRT;

	if (sEnableGIDebugOutput == 1)
	{
		debugBlitColor(queue, giCtx.historyDiffuseRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 2)
	{
		debugBlitColor(queue, probeTraceRadianceRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 3)
	{
		debugBlitColor(queue, probeReprojectTraceRadianceRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 4)
	{
		debugBlitColor(queue, probeReprojectStatRadianceRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 5)
	{
		debugBlitColor(queue, filteredRadiusRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 6)
	{
		debugBlitColor(queue, specularTraceRadianceRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 7)
	{
		debugBlitColor(queue, reprojectSpecularRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 8)
	{
		debugBlitColor(queue, giCtx.historySpecularRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 9)
	{
		debugBlitColor(queue, probeReprojectStatSpecularRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 10)
	{
		debugBlitColor(queue, specularTraceRadiancePostFilterRT, gbuffers.color);
	}
	else if (sEnableGIDebugOutput == 11 && AORT != nullptr)
	{
		debugBlitColor(queue, AORT, gbuffers.color);
	}
}