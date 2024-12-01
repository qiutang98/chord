#include <renderer/lighting.h>
#include <renderer/renderer.h>
#include <shader/gi.h>
#include <shader/gi_screen_probe_spawn.hlsl>
#include <shader/gi_screen_probe_trace.hlsl>
#include <shader/gi_screen_probe_project_sh.hlsl>
#include <shader/gi_screen_probe_interpolate.hlsl>
#include <shader/gi_world_probe_sh_inject.hlsl>
#include <shader/gi_world_probe_sh_propagate.hlsl>
#include <shader/gi_denoise.hlsl>
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
PRIVATE_GLOBAL_SHADER(GIWorldProbeSHInjectCS, "resource/shader/gi_world_probe_sh_inject.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(GIWorldProbeSHPropagateCS, "resource/shader/gi_world_probe_sh_propagate.hlsl", "mainCS", EShaderStage::Compute);

PRIVATE_GLOBAL_SHADER(GIDenoiseCS, "resource/shader/gi_denoise.hlsl", "mainCS", EShaderStage::Compute);

graphics::PoolTextureRef chord::giUpdate(
	graphics::CommandList& cmd, 
	graphics::GraphicsQueue& queue, 
	const AtmosphereLut& luts, 
	const CascadeShadowContext& cascadeCtx, 
	GIWorldProbeContext& giWorldProbeCtx,
	GBufferTextures& gbuffers, 
	uint32 cameraViewId, 
	graphics::helper::AccelKHRRef tlas, 
	ICamera* camera)
{
	if (!sEnableGI || !getContext().isRaytraceSupport())
	{
		giWorldProbeCtx = {};
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
		static constexpr int   kProbeDim     = 64;
		static constexpr float kVoxelSize    = 0.5f; // 0.5; 1.0; 2.0; 4.0; 8.0; 16.0; 32.0; 64.0;
		{
			float voxelSize = kVoxelSize;
			for (int i = 0; i < kCascadeCount; i++)
			{
				if (giWorldProbeCtx.volumes.size() <= i)
				{
					giWorldProbeCtx.volumes.push_back({});
					giWorldProbeCtx.volumes[i].scrollAnchor = camera->getPosition();

					// 
					bHistoryInvalid = true;
				}

				auto& resource = giWorldProbeCtx.volumes[i];
				bHistoryInvalid
					|= (resource.probeDim.x            != kProbeDim)
					|| (resource.probeSpacing.x        != voxelSize)
					|| (resource.probeIrradianceBuffer == nullptr)
					|| (resource.probeDirectionBuffer  == nullptr);

				voxelSize *= 2.0f;
			}
		}

		//
		{
			float voxelSize = kVoxelSize;
			for (int i = 0; i < kCascadeCount; i++)
			{
				auto& resource = giWorldProbeCtx.volumes[i];
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

					resource.probeDirectionBuffer = bufferPool.createGPUOnly("GI-WorldProbe-SH-Direction",
						sizeof(uint) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
						VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

					queue.clearUAV(resource.probeIrradianceBuffer, 0U);
					queue.clearUAV(resource.probeDirectionBuffer, 0U);
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
				auto& resource = giWorldProbeCtx.volumes[i];

				worldProbeConfigs[i].probeDim = resource.probeDim;
				worldProbeConfigs[i].sh_UAV = asUAV(queue, resource.probeIrradianceBuffer);

				worldProbeConfigs[i].probeSpacing = resource.probeSpacing;
				worldProbeConfigs[i].sh_SRV = asSRV(queue, resource.probeIrradianceBuffer);

				worldProbeConfigs[i].bRestAll = bHistoryInvalid;
				worldProbeConfigs[i].sh_direction_UAV = asUAV(queue, resource.probeDirectionBuffer);
				worldProbeConfigs[i].sh_direction_SRV = asSRV(queue, resource.probeDirectionBuffer);

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

		// SH propagate. 
		{
			// 
			uint bufferSize = sizeof(SH3_gi_pack) * kProbeDim * kProbeDim * kProbeDim;
			auto probeIrradianceTempBuffer = bufferPool.createGPUOnly("GI-WorldProbe-SH-Irradiance-Temp",
				bufferSize,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

	

			for (int i = kCascadeCount - 1; i >=0 ; i--)
			{
				auto& resource = giWorldProbeCtx.volumes[i];

				queue.copyBuffer(resource.probeIrradianceBuffer, probeIrradianceTempBuffer, bufferSize, 0, 0);

				GIWorldProbeSHPropagatePushConsts pushConst{};
				pushConst.cameraViewId = cameraViewId;
				pushConst.clipmapConfigBufferId = worldProbeConfigBufferId;
				pushConst.clipmapLevel = i;

				pushConst.sh_srv = asSRV(queue, probeIrradianceTempBuffer);
				pushConst.sh_uav = asUAV(queue, resource.probeIrradianceBuffer);
				pushConst.bLastCascade = i == kCascadeCount - 1;

				const uint dispatchDim = divideRoundingUp(kProbeDim * kProbeDim * kProbeDim, 64);

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


	uint2 halfDim = gbuffers.dimension / 2u;
	uint2 screenProbeDim = halfDim / 8u; // divideRoundingUp(halfDim, uint2(8));

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
		pushConsts.gbufferDim        = halfDim;

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

		pushConsts.minRayTraceDistance = 0.01f;
		pushConsts.maxRayTraceDistance = 800.0f;
		pushConsts.cameraViewId        = cameraViewId;
		pushConsts.probeSpawnInfoSRV   = asSRV(queue, newScreenProbeSpawnInfoBuffer);
		pushConsts.rayHitLODOffset     = 2.0f;
		pushConsts.radianceUAV      = asUAV(queue, screenProbeTraceRadianceRT);

		for (int i = 0; i < worldProbeCascadeCount; i++)
		{
			asSRV(queue, giWorldProbeCtx.volumes[i].probeDirectionBuffer);
			asSRV(queue, giWorldProbeCtx.volumes[i].probeIrradianceBuffer);
		}

		pushConsts.clipmapConfigBufferId = worldProbeConfigBufferId;
		pushConsts.clipmapCount = worldProbeCascadeCount;
		pushConsts.bHistoryValid = !bHistoryInvalid;

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

	auto newScreenProbeSHBuffer = bufferPool.createGPUOnly("GI-ScreenProbe-SH", sizeof(SH3_gi_pack) * screenProbeDim.x * screenProbeDim.y, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
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
			asUAV(queue, giWorldProbeCtx.volumes[i].probeDirectionBuffer);
			asUAV(queue, giWorldProbeCtx.volumes[i].probeIrradianceBuffer);
		}

		check(worldProbeCascadeCount == 2 || worldProbeCascadeCount == 4 || worldProbeCascadeCount == 8);

		// One workgroup can handle probe count (included all cascade).
		uint worldGroupHandleProbe = 64 / worldProbeCascadeCount;
		const uint dispatchDim = divideRoundingUp(screenProbeDim.x * screenProbeDim.y, worldGroupHandleProbe);

		auto computeShader = getContext().getShaderLibrary().getShader<GIWorldProbeSHInjectCS>();
		addComputePass2(queue,
			"GI: WorldProbeInject",
			getContext().computePipe(computeShader, "GI: WorldProbeInject"),
			pushConsts,
			{ dispatchDim, 1, 1 });

		for (int i = 0; i < worldProbeCascadeCount; i++)
		{
			asSRV(queue, giWorldProbeCtx.volumes[i].probeDirectionBuffer);
			asSRV(queue, giWorldProbeCtx.volumes[i].probeIrradianceBuffer);
		}
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
		pushConsts.clipmapConfigBufferId = worldProbeConfigBufferId;
		pushConsts.clipmapCount = worldProbeCascadeCount;

		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));

		auto computeShader = getContext().getShaderLibrary().getShader<GIScreenProbeInterpolateCS>();
		addComputePass2(queue,
			"GI: Interpolate",
			getContext().computePipe(computeShader, "GI: Interpolate"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });

		
	}

	{

		GIDenoisePushConsts pushConst{};

		const uint2 dispatchDim = divideRoundingUp(halfDim, uint2(8));
		pushConst.dim = halfDim;
		pushConst.firstFrame = false;
		if (giWorldProbeCtx.historyRT == nullptr)
		{
			pushConst.firstFrame = true;
			giWorldProbeCtx.historyRT = rtPool.create("GI-ScreenProbe-Interpolate", halfDim.x, halfDim.y, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		
			
		}

		pushConst.srv = asSRV(queue, giInterpolateRT);
		pushConst.uav = asUAV(queue, giWorldProbeCtx.historyRT);

		auto computeShader = getContext().getShaderLibrary().getShader<GIDenoiseCS>();
		addComputePass2(queue,
			"GI: Denoise",
			getContext().computePipe(computeShader, "GI: Denoise"),
			pushConst,
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
	else if (sEnableGIDebugOutput == 3)
	{
		return giWorldProbeCtx.historyRT;
	}

	return nullptr;
}