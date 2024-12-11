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


graphics::PoolTextureRef chord::giUpdate(
	graphics::CommandList& cmd,
	graphics::GraphicsQueue& queue,
	const AtmosphereLut& luts,
	const CascadeShadowContext& cascadeCtx,
	GIContext& giCtx,
	GBufferTextures& gbuffers,
	uint32 cameraViewId,
	graphics::helper::AccelKHRRef tlas,
	graphics::PoolTextureRef disocclusionMask,
	ICamera* camera,
	graphics::PoolTextureRef depth_Half_LastFrame,
	graphics::PoolTextureRef pixelNormalRS_Half_LastFrame,
	bool bCameraCut,
	RendererTimerLambda timer)
{
	if (!sEnableGI || !getContext().isRaytraceSupport())
	{
		giCtx = { };
		bCameraCut = true;

		return nullptr;
	}

	auto& rtPool = getContext().getTexturePool();
	auto& bufferPool = getContext().getBufferPool();

	// Allocate world probe resource. 
	uint32 worldProbeConfigBufferId;
	uint32 worldProbeCascadeCount;
	bool bHistoryInvalid = false;
	{
		static constexpr int   kCascadeCount = 8;
		static constexpr int   kProbeDim = 64;
		static constexpr float kVoxelSize = kMinProbeSpacing; // 0.5; 1.0; 2.0; 4.0; 8.0; 16.0; 32.0; 64.0;
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
					|= (resource.probeDim.x != kProbeDim)
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
					resource.probeDim = { kProbeDim, kProbeDim, kProbeDim };
					resource.probeSpacing = { voxelSize, voxelSize, voxelSize };
				}
				else
				{
					check(resource.probeSpacing.x == voxelSize);
					check(resource.probeDim.x == kProbeDim);
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

			const uint dispatchDim = divideRoundingUp(kProbeDim * kProbeDim * kProbeDim, 64);

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

		tracePushConsts.depthId = asSRV(queue, gbuffers.depth_Half);
		tracePushConsts.normalRSId = asSRV(queue, gbuffers.pixelRSNormal_Half);
		tracePushConsts.roughnessId = asSRV(queue, gbuffers.roughness_Half);
		tracePushConsts.UAV = asUAV(queue, specularTraceRadianceRT);
		tracePushConsts.disocclusionMask = disocclusionMask != nullptr ? asSRV(queue, disocclusionMask) : kUnvalidIdUint32; //

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

	if (giCtx.historyDiffuseRT != nullptr && disocclusionMask != nullptr &&
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
		pushConst.disoccludedMaskSRV = asSRV(queue, disocclusionMask);
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
		return giCtx.historyDiffuseRT;
	}
	else if (sEnableGIDebugOutput == 2)
	{
		return probeTraceRadianceRT;
	}
	else if (sEnableGIDebugOutput == 3)
	{
		return probeReprojectTraceRadianceRT;
	}
	else if (sEnableGIDebugOutput == 4)
	{
		return probeReprojectStatRadianceRT;
	}
	else if (sEnableGIDebugOutput == 5)
	{
		return filteredRadiusRT;
	}
	else if (sEnableGIDebugOutput == 6)
	{
		return specularTraceRadianceRT;
	}
	else if (sEnableGIDebugOutput == 7)
	{
		return reprojectSpecularRT;
	}
	else if (sEnableGIDebugOutput == 8)
	{
		return giCtx.historySpecularRT;
	}
	else if (sEnableGIDebugOutput == 9)
	{
		return probeReprojectStatSpecularRT;
	}
	else if (sEnableGIDebugOutput == 10)
	{
		return specularTraceRadiancePostFilterRT;
	}

	return nullptr;
}