#include <shader/ddgi_probe_trace.hlsl>
#include <renderer/lighting.h>
#include <renderer/renderer.h>
#include <shader/ddgi.h>
#include <shader/ddgi_probe_convolution.hlsl>
#include <shader/ddgi_probe_debug_sample.hlsl>
#include <shader/ddgi_clipmap_update.hlsl>
#include <shader/ddgi_relighting.hlsl>
#include <shader/ddgi_relocation.hlsl>
// 
#include <random>

using namespace chord;
using namespace chord::graphics;

static uint32 sEnableDDGI = 0;
static AutoCVarRef cVarEnableDDGI(
	"r.ddgi",
	sEnableDDGI,
	"Enable ddgi or not."
);

static uint32 sEnableDDGIDebugOutput = 0;
static AutoCVarRef cVarEnableDDGIDebugOutput(
	"r.ddgi.debug",
	sEnableDDGIDebugOutput,
	"Enable ddgi debug output or not."
);

PRIVATE_GLOBAL_SHADER(DDGIClipmapUpdateClearMarkerBufferCS, "resource/shader/ddgi_clipmap_update.hlsl", "clearMarkerBufferCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIClipmapUpdateIndirectCmdParamCS, "resource/shader/ddgi_clipmap_update.hlsl", "indirectCmdParamCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIClipmapUpdateInvalidProbeTracePass_0_CS, "resource/shader/ddgi_clipmap_update.hlsl", "updateInvalidProbeTracePass_0_CS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIClipmapUpdateInvalidProbeTracePass_1_CS, "resource/shader/ddgi_clipmap_update.hlsl", "updateInvalidProbeTracePass_1_CS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIProbeTraceCS, "resource/shader/ddgi_probe_trace.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIProbeRelightingCS, "resource/shader/ddgi_relighting.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIClipmapUpdateAppendRelightingCS, "resource/shader/ddgi_clipmap_update.hlsl", "appendRelightingProbeCountCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIConvolutionIndirectCmdParamCS, "resource/shader/ddgi_probe_convolution.hlsl", "indirectCmdParamCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIDebugSampleCS, "resource/shader/ddgi_probe_debug_sample.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIClipmapCopyValidCounterCS, "resource/shader/ddgi_clipmap_update.hlsl", "copyValidCounterCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIRelocationCS, "resource/shader/ddgi_relocation.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(DDGIRelocationIndirectCmdCS, "resource/shader/ddgi_relocation.hlsl", "indirectCmdParamCS", EShaderStage::Compute);

class DDGIConvolutionCS : public GlobalShader
{
public:
	DECLARE_SUPER_TYPE(GlobalShader);

	class SV_bIrradiance : SHADER_VARIANT_BOOL("DDGI_BLEND_DIM_IRRADIANCE");
	using Permutation = TShaderVariantVector<SV_bIrradiance>;
};
IMPLEMENT_GLOBAL_SHADER(DDGIConvolutionCS, "resource/shader/ddgi_probe_convolution.hlsl", "mainCS", EShaderStage::Compute);

//
static std::default_random_engine gen;
static std::uniform_real_distribution<float> distribution(0.0f, 1.0f);

static inline float getRandomFloat()
{
	return distribution(gen);
}

graphics::PoolTextureRef chord::ddgiUpdate(
	graphics::CommandList& cmd,
	graphics::GraphicsQueue& queue,
	const AtmosphereLut& luts,
	const DDGIConfigCPU& ddgiConfig,
	const CascadeShadowContext& cascadeCtx,
	GBufferTextures& gbuffers,
	DDGIContext& ddgiCtx,
	uint32 cameraViewId,
	graphics::helper::AccelKHRRef tlas,
	ICamera* camera,
	graphics::PoolTextureRef hzb)
{
	if (!sEnableDDGI || !getContext().isRaytraceSupport())
	{
		// Clear all resource of ddgi. 
		ddgiCtx = {};
		return nullptr;
	}

	std::array<DDGIVoulmeConfig, kDDGICsacadeCount> configs { };

	static constexpr float kProbeSpacings[kDDGICsacadeCount] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f  };
	static constexpr int3 kProbeDims[kDDGICsacadeCount] =
	{  
		{ 32, 8, 32 },  
		{ 32, 8, 32 },  
		{ 32, 8, 32 }, 
		{ 32, 8, 32 },  
		{ 32, 8, 32 },  
		{ 32, 8, 32 },  
		{ 32, 8, 32 },   
		{ 32, 8, 32 },   
	};
	static constexpr int kProbeUpdateMaxCounts[kDDGICsacadeCount] = { 1024, 512, 256, 128, 64, 32, 16, 8 }; // 
	static constexpr int kProbeUpdateRelightMaxCounts[kDDGICsacadeCount] = { 2048, 1024, 512, 256, 128, 64, 32, 16 }; // 
	// 
	for (uint32 i = 0; i < kDDGICsacadeCount; i++)
	{
		const auto& cpuConfig = ddgiConfig.volumeConfigs[i];

		configs[i].probeDim = kProbeDims[i];
		configs[i].probeSpacing = float3{ 1.0f, 1.0f, 1.0f } * kProbeSpacings[i];
		configs[i].rayHitSampleTextureLod = cpuConfig.rayHitSampleTextureLod;

		//
		configs[i].rayTraceStartDistance = 0.0f;
		configs[i].rayTraceMaxDistance = 1e27f;

		// 
		configs[i].linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
		configs[i].hysteresis = cpuConfig.hysteresis; 

		//
		configs[i].probeNormalBias = cpuConfig.probeNormalBias;
		configs[i].probeViewBias   = cpuConfig.probeViewBias;

		// 
		configs[i].probeDistanceExponent = cpuConfig.probeDistanceExponent;
		configs[i].probeMinFrontfaceDistance = kProbeSpacings[i] * 0.5f;

		// At least 4 frame when post invalid.
		configs[i].probeFrameFill = 2;
	}

	bool bHistoryInvalid = false;
	for (uint32 i = 0; i < kDDGICsacadeCount; i++)
	{
		if (ddgiCtx.volumes.size() <= i)
		{
			// Create new volume.
			ddgiCtx.volumes.push_back({});

			// Init use camera position.
			ddgiCtx.volumes[i].scrollAnchor = camera->getPosition();
		}

		const auto& config = configs[i];
		auto& resource = ddgiCtx.volumes[i];

		bHistoryInvalid
			|= (resource.probeDim != config.probeDim)
			|| (resource.probeSpacing != config.probeSpacing)
			|| (resource.iradianceTexture == nullptr)
			|| (resource.distanceTexture == nullptr)
			|| (resource.probeTraceMarkerBuffer == nullptr)
			|| (resource.probeTraceGbufferInfoBuffer == nullptr)
			|| (resource.probeTraceInfoBuffer == nullptr)
			|| (resource.probeTraceHistoryValidBuffer == nullptr)
			|| (resource.probeOffsetBuffer == nullptr)
			|| (resource.probeTracedFrameBuffer == nullptr);
	}

	for (uint32 i = 0; i < kDDGICsacadeCount; i++)
	{
		const auto& config = configs[i];
		auto& resource = ddgiCtx.volumes[i];

		if (bHistoryInvalid)
		{
			resource.probeDim = config.probeDim;
			resource.probeSpacing = config.probeSpacing;
			
			resource.iradianceTexture = getContext().getTexturePool().create(
				"DDGI-Irradiance", 
				resource.probeDim.x * kDDGIProbeIrradianceTexelNum,
				resource.probeDim.y * resource.probeDim.z * kDDGIProbeIrradianceTexelNum,
				VK_FORMAT_B10G11R11_UFLOAT_PACK32,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

			resource.distanceTexture = getContext().getTexturePool().create(
				"DDGI-Distance",
				resource.probeDim.x * kDDGIProbeDistanceTexelNum,
				resource.probeDim.y * resource.probeDim.z * kDDGIProbeDistanceTexelNum,
				VK_FORMAT_R32G32_SFLOAT,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

			//
			static const auto clearValue = VkClearColorValue{ .uint32 = { 0, 0, 0, 0} };
			static const auto range = helper::buildBasicImageSubresource();

			// Clean image.
			queue.clearImage(resource.iradianceTexture, &clearValue, 1, &range);
			queue.clearImage(resource.distanceTexture, &clearValue, 1, &range);

			// Per probe buffer.
			resource.probeTraceMarkerBuffer = getContext().getBufferPool().createGPUOnly(
				"DDGI-ProbeTraceMarker",
				sizeof(uint) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

			// Per ray buffer. 
			resource.probeTraceGbufferInfoBuffer = getContext().getBufferPool().createGPUOnly(
				"DDGI-ProbeTraceGBuffer",
				sizeof(DDGIProbeCacheMiniGbuffer) * kDDGIPerProbeRayCount * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

			// Per probe buffer. 
			resource.probeTraceInfoBuffer = getContext().getBufferPool().createGPUOnly(
				"DDGI-ProbeTraceInfo",
				sizeof(DDGIProbeCacheTraceInfo) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

			resource.probeTraceHistoryValidBuffer = getContext().getBufferPool().createGPUOnly(
				"DDGI-probeTraceHistoryValidBuffer",
				sizeof(uint) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

			resource.probeOffsetBuffer = getContext().getBufferPool().createGPUOnly(
				"DDGI-probeOffsetBuffer",
				sizeof(float3) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

			resource.probeTracedFrameBuffer = getContext().getBufferPool().createGPUOnly(
				"DDGI-probeTracedFrameBuffer",
				sizeof(uint) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		}

		// 
		configs[i].bHistoryValid = !bHistoryInvalid;

		//
		configs[i].distanceSRV         = asSRV(queue, resource.distanceTexture);
		configs[i].irradianceSRV       = asSRV(queue, resource.iradianceTexture);
		configs[i].distanceTexelSize   = float2(1.0f / float(resource.probeDim.x * kDDGIProbeDistanceTexelNum), 1.0f / float(kDDGIProbeDistanceTexelNum * resource.probeDim.y * resource.probeDim.z));
		configs[i].irradianceTexelSize = float2(1.0f / float(resource.probeDim.x * kDDGIProbeIrradianceTexelNum), 1.0f / float(kDDGIProbeIrradianceTexelNum * resource.probeDim.y * resource.probeDim.z));
		configs[i].probeHistoryValidSRV = asSRV(queue, resource.probeTraceHistoryValidBuffer);
		configs[i].offsetSRV = asSRV(queue, resource.probeOffsetBuffer);
		configs[i].probeCacheInfoSRV = asSRV(queue, resource.probeTraceInfoBuffer);

		// Scrolling.
		if (bHistoryInvalid)
		{
			configs[i].currentScrollOffset = { 0, 0, 0 };
			configs[i].scrollOffset  = { 0, 0, 0 };
			configs[i].probeCenterRS = { 0.0f, 0.0f, 0.0f };
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

			configs[i].currentScrollOffset = scroll;
			resource.scrollOffset += scroll;

			// Update anchor.
			resource.scrollAnchor -= double3(resource.probeSpacing * float3(scroll));

			// Update scroll offset.
			configs[i].scrollOffset = resource.scrollOffset % int3(resource.probeDim);

			// 
			double3 newTranslation = resource.scrollAnchor - camera->getPosition();
			configs[i].probeCenterRS = float3(newTranslation);
		}
	}

	// 
	const uint32 ddgiConfigBufferId = uploadBufferToGPU(cmd, "DDGIVolumeConfigs", configs.data(), configs.size()).second;

	// 
	for (uint32 ddgiVolumeId = 0; ddgiVolumeId < kDDGICsacadeCount; ddgiVolumeId++)
	{
		const auto& volume = configs.at(ddgiVolumeId);
		const auto& resource = ddgiCtx.volumes[ddgiVolumeId];
		const auto& cpuConfig = ddgiConfig.volumeConfigs[ddgiVolumeId];

		asSRV(queue, resource.iradianceTexture);
		asSRV(queue, resource.distanceTexture);

		// Clear marker buffer if first time create.
		if (!volume.bHistoryValid)
		{
			DDGIClipmapUpdatePushConsts pushConsts{};
			pushConsts.ddgiConfigBufferId = ddgiConfigBufferId;
			pushConsts.ddgiConfigId = ddgiVolumeId;
			pushConsts.probeTracedMarkUAV = asUAV(queue, resource.probeTraceMarkerBuffer);
			pushConsts.probeHistoryValidUAV = asUAV(queue, resource.probeTraceHistoryValidBuffer);
			pushConsts.probeTraceFrameUAV = asUAV(queue, resource.probeTracedFrameBuffer);

			auto computeShader = getContext().getShaderLibrary().getShader<DDGIClipmapUpdateClearMarkerBufferCS>();

			// Per-probe dispatch.
			int dispatchCount = (volume.probeDim.x * volume.probeDim.y * volume.probeDim.z + 63) / 64;
			addComputePass2(queue,
				"DDGI: clearMarkerBufferCS",
				getContext().computePipe(computeShader, "DDGI: clearMarkerBuffer"),
				pushConsts,
				{ dispatchCount, 1, 1 });

			asSRV(queue, resource.probeTraceMarkerBuffer);
		}

		// 
		auto probeTraceCounterBuffer = getContext().getBufferPool().createGPUOnly("DDGI-ProbeTraceCounter", sizeof(uint) * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		queue.clearUAV(probeTraceCounterBuffer);

		// 
		auto probeUpdateLinearIndexBuffer = getContext().getBufferPool().createGPUOnly("DDGI-ProbeUpdateLinearIndex", sizeof(uint) * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		{
			DDGIClipmapUpdatePushConsts pushConsts{};
			pushConsts.currentScrollOffset = volume.currentScrollOffset;
			pushConsts.cameraViewId        = cameraViewId;
			pushConsts.bClearAll           = !volume.bHistoryValid;
			pushConsts.ddgiConfigBufferId  = ddgiConfigBufferId;
			pushConsts.ddgiConfigId        = ddgiVolumeId;

			//
			pushConsts.maxProbeUpdatePerFrame    = kProbeUpdateMaxCounts[ddgiVolumeId];
			pushConsts.probeUpdateMod            = volume.probeDim.x * volume.probeDim.y * volume.probeDim.z / pushConsts.maxProbeUpdatePerFrame;
			pushConsts.probeUpdateEqual          = getFrameCounter() % pushConsts.probeUpdateMod;
			pushConsts.probeUpdateFrameThreshold = math::max(pushConsts.probeUpdateMod / 8U, 1U);

			// Per-probe dispatch.
			int dispatchCount = (volume.probeDim.x * volume.probeDim.y * volume.probeDim.z + 63) / 64;

			pushConsts.probeTracedMarkUAV        = asUAV(queue, resource.probeTraceMarkerBuffer);
			pushConsts.probeCounterUAV           = asUAV(queue, probeTraceCounterBuffer);
			pushConsts.probeUpdateLinearIndexUAV = asUAV(queue, probeUpdateLinearIndexBuffer);
			pushConsts.probeHistoryValidUAV      = asUAV(queue, resource.probeTraceHistoryValidBuffer);
			pushConsts.probeTraceFrameUAV        = asUAV(queue, resource.probeTracedFrameBuffer);

			addComputePass2(queue,
				"DDGI: ClipmapUpdateInvalidProbeTracePass_0_CS",
				getContext().computePipe(getContext().getShaderLibrary().getShader<DDGIClipmapUpdateInvalidProbeTracePass_0_CS>(), "DDGI: DDGIClipmapUpdateInvalidProbeTracePass_0"),
				pushConsts,
				{ dispatchCount, 1, 1 });

			pushConsts.probeTracedMarkUAV = asUAV(queue, resource.probeTraceMarkerBuffer);
			pushConsts.probeCounterUAV = asUAV(queue, probeTraceCounterBuffer);
			pushConsts.probeUpdateLinearIndexUAV = asUAV(queue, probeUpdateLinearIndexBuffer);
			pushConsts.probeHistoryValidUAV = asUAV(queue, resource.probeTraceHistoryValidBuffer);
			pushConsts.probeTraceFrameUAV = asUAV(queue, resource.probeTracedFrameBuffer);

			addComputePass2(queue,
				"DDGI: ClipmapUpdateInvalidProbeTracePass_1_CS",
				getContext().computePipe(getContext().getShaderLibrary().getShader<DDGIClipmapUpdateInvalidProbeTracePass_1_CS>(), "DDGI: DDGIClipmapUpdateInvalidProbeTracePass_1"),
				pushConsts,
				{ dispatchCount, 1, 1 });
		}

		// Relocation if history valid.
		{
			auto cmdBuffer = getContext().getBufferPool().createGPUOnly("cmdBuffer", sizeof(uint4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
			{
				DDGIRelocationPushConsts pushConst{};
				pushConst.probeCounterSRV = asSRV(queue, probeTraceCounterBuffer);
				pushConst.UAV = asUAV(queue, cmdBuffer);

				auto computeShader = getContext().getShaderLibrary().getShader<DDGIRelocationIndirectCmdCS>();
				addComputePass2(queue,
					"DDGI: relocationIndirectCmdCS",
					getContext().computePipe(computeShader, "DDGI: relocationIndirectCmd"),
					pushConst,
					{ 1, 1, 1 });
			}

			DDGIRelocationPushConsts pushConst{};
			pushConst.cameraViewId = cameraViewId;
			pushConst.ddgiConfigBufferId = ddgiConfigBufferId;
			pushConst.ddgiConfigId = ddgiVolumeId;
			pushConst.probeCounterSRV = asSRV(queue, probeTraceCounterBuffer);
			pushConst.probeTraceLinearIndexSRV = asSRV(queue, probeUpdateLinearIndexBuffer);
			pushConst.probeHistoryValidSRV = asSRV(queue, resource.probeTraceHistoryValidBuffer);
			pushConst.probeCacheRayGbufferSRV = asSRV(queue, resource.probeTraceGbufferInfoBuffer);
			pushConst.probeCacheInfoSRV = asSRV(queue, resource.probeTraceInfoBuffer);
			pushConst.UAV = asUAV(queue, resource.probeOffsetBuffer);

			auto computeShader = getContext().getShaderLibrary().getShader<DDGIRelocationCS>();
			addIndirectComputePass2(queue,
				"DDGI: RelocationCS",
				getContext().computePipe(computeShader, "DDGI: Relocation"),
				pushConst,
				cmdBuffer);

			asSRV(queue, resource.probeOffsetBuffer);
		}

		// Trace. 
		{
			auto traceCmdBuffer = getContext().getBufferPool().createGPUOnly("traceCmdBuffer", sizeof(uint4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
			{
				DDGIClipmapUpdatePushConsts pushConsts{};
				pushConsts.probeCounterSRV = asSRV(queue, probeTraceCounterBuffer);
				pushConsts.cmdBufferId = asUAV(queue, traceCmdBuffer);

				auto computeShader = getContext().getShaderLibrary().getShader<DDGIClipmapUpdateIndirectCmdParamCS>();
				addComputePass2(queue,
					"DDGI: traceFillIndirectBufferCS",
					getContext().computePipe(computeShader, "DDGI: traceFillIndirectBuffer"),
					pushConsts,
					{ 1, 1, 1 });
			}

			DDGITracePushConsts pushConst{};
			pushConst.randomRotation = math::toMat4(glm::quat(
			{
				getRandomFloat() * 2.0f * math::pi<float>(),
				getRandomFloat() * 2.0f * math::pi<float>(),
				getRandomFloat() * 2.0f * math::pi<float>(),
			}));
			pushConst.cameraViewId = cameraViewId;
			pushConst.ddgiConfigBufferId = ddgiConfigBufferId;
			pushConst.ddgiConfigId = ddgiVolumeId;
			pushConst.probeCacheInfoUAV        = asUAV(queue, resource.probeTraceInfoBuffer);
			pushConst.probeCacheRayGbufferUAV  = asUAV(queue, resource.probeTraceGbufferInfoBuffer);
			pushConst.probeTraceLinearIndexSRV = asSRV(queue, probeUpdateLinearIndexBuffer);
			pushConst.probeTracedMarkSRV       = asSRV(queue, resource.probeTraceMarkerBuffer);

			auto computeShader = getContext().getShaderLibrary().getShader<DDGIProbeTraceCS>();
			addIndirectComputePass(queue,
				"DDGI: traceCS",
				getContext().computePipe(computeShader, "DDGI: trace", {
					getContext().descriptorFactoryBegin()
					.accelerateStructure(0) // TLAS
					.buildNoInfoPush() }),
				traceCmdBuffer, 0,
				[&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
				{
					pipe->pushConst(cmd, pushConst);

					PushSetBuilder(queue, cmd)
						.addAccelerateStructure(tlas)
						.push(pipe, 1); // Push set 1.
				});
		}

		// Distance update.
		{
			auto traceCmdBuffer = getContext().getBufferPool().createGPUOnly("traceCmdBuffer", sizeof(uint4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
			{
				DDGIConvolutionPushConsts pushConsts{};
				pushConsts.probeCounterSRV = asSRV(queue, probeTraceCounterBuffer);
				pushConsts.cmdBufferId     = asUAV(queue, traceCmdBuffer);

				auto computeShader = getContext().getShaderLibrary().getShader<DDGIConvolutionIndirectCmdParamCS>();
				addComputePass2(queue,
					"DDGI: ConvolutionIndirectBufferCS",
					getContext().computePipe(computeShader, "DDGI: ConvolutionIndirectBuffer"),
					pushConsts,
					{ 1, 1, 1 });
			}


			DDGIConvolutionPushConsts distanceConst{};
			distanceConst.cameraViewId = cameraViewId;
			distanceConst.ddgiConfigBufferId = ddgiConfigBufferId;
			distanceConst.ddgiConfigId = ddgiVolumeId;

			distanceConst.UAV = asUAV(queue, resource.distanceTexture);
			distanceConst.probeTraceLinearIndexSRV = asSRV(queue, probeUpdateLinearIndexBuffer);
			distanceConst.probeTracedMarkSRV = asSRV(queue, resource.probeTraceMarkerBuffer);
			distanceConst.probeCacheRayGbufferSRV = asSRV(queue, resource.probeTraceGbufferInfoBuffer);
			distanceConst.probeCacheInfoSRV = asSRV(queue, resource.probeTraceInfoBuffer);

			// 
			distanceConst.probeHistoryValidUAV = asUAV(queue, resource.probeTraceHistoryValidBuffer);

			DDGIConvolutionCS::Permutation permutation;
			permutation.set<DDGIConvolutionCS::SV_bIrradiance>(false);
			auto computeShader = getContext().getShaderLibrary().getShader<DDGIConvolutionCS>(permutation);

			addIndirectComputePass2(queue,
				"DDGI: blendDistanceCS",
				getContext().computePipe(computeShader, "DDGI: blendDistanceCS"),
				distanceConst,
				traceCmdBuffer);

			asSRV(queue, resource.distanceTexture);
		}

		// Relighting append. 
		auto radianceBuffer = getContext().getBufferPool().createGPUOnly(
			"DDGI-Radiance",
			sizeof(float3) * kDDGIPerProbeRayCount * resource.probeDim.x * resource.probeDim.y * resource.probeDim.z,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);


		{
			{
				DDGIClipmapUpdatePushConsts pushConsts{};
				pushConsts.probeCounterUAV = asUAV(queue, probeTraceCounterBuffer);


				addComputePass2(queue,
					"DDGI: DDGIClipmapCopyValidCounterCS",
					getContext().computePipe(getContext().getShaderLibrary().getShader<DDGIClipmapCopyValidCounterCS>(), "DDGI: DDGIClipmapCopyValidCounter"),
					pushConsts,
					{ 1, 1, 1 });
			}


			DDGIClipmapUpdatePushConsts pushConsts{};
			pushConsts.currentScrollOffset = volume.currentScrollOffset;
			pushConsts.cameraViewId        = cameraViewId;
			pushConsts.bClearAll           = !volume.bHistoryValid;
			pushConsts.ddgiConfigBufferId  = ddgiConfigBufferId;
			pushConsts.ddgiConfigId        = ddgiVolumeId;

			// 
			pushConsts.maxProbeUpdatePerFrame = kProbeUpdateRelightMaxCounts[ddgiVolumeId];
			pushConsts.probeUpdateMod = volume.probeDim.x * volume.probeDim.y * volume.probeDim.z / pushConsts.maxProbeUpdatePerFrame;
			pushConsts.probeUpdateEqual = getFrameCounter() % pushConsts.probeUpdateMod;
			pushConsts.probeUpdateFrameThreshold = math::max(pushConsts.probeUpdateMod / 8U, 1U);

			// Per-probe dispatch.
			int dispatchCount = (volume.probeDim.x * volume.probeDim.y * volume.probeDim.z + 63) / 64;

			pushConsts.probeTracedMarkUAV        = asUAV(queue, resource.probeTraceMarkerBuffer);
			pushConsts.probeCounterUAV           = asUAV(queue, probeTraceCounterBuffer);
			pushConsts.probeUpdateLinearIndexUAV = asUAV(queue, probeUpdateLinearIndexBuffer);

			addComputePass2(queue,
				"DDGI: DDGIClipmapUpdateAppendRelightingCS",
				getContext().computePipe(getContext().getShaderLibrary().getShader<DDGIClipmapUpdateAppendRelightingCS>(), "DDGI: DDGIClipmapUpdateAppendRelighting"),
				pushConsts,
				{ dispatchCount, 1, 1 });
		}

		{
			auto relightingCmdBuffer = getContext().getBufferPool().createGPUOnly("relightingCmdBuffer", sizeof(uint4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
			{
				DDGIClipmapUpdatePushConsts pushConsts{};
				pushConsts.probeCounterSRV = asSRV(queue, probeTraceCounterBuffer);
				pushConsts.cmdBufferId = asUAV(queue, relightingCmdBuffer);

				auto computeShader = getContext().getShaderLibrary().getShader<DDGIClipmapUpdateIndirectCmdParamCS>();
				addComputePass2(queue,
					"DDGI: relightingIndirectBufferCS",
					getContext().computePipe(computeShader, "DDGI: relightingIndirectBuffer"),
					pushConsts,
					{ 1, 1, 1 });
			}

			// 
			DDGIRelightingPushConsts pushConst{};
			pushConst.cameraViewId             = cameraViewId;
			pushConst.ddgiConfigBufferId       = ddgiConfigBufferId;
			pushConst.ddgiConfigId             = ddgiVolumeId;
			pushConst.ddgiCount                = kDDGICsacadeCount;
			pushConst.cascadeCount             = cascadeCtx.depths.size();
			pushConst.shadowViewId             = cascadeCtx.viewsSRV;
			pushConst.shadowDepthIds           = cascadeCtx.cascadeShadowDepthIds;
			pushConst.transmittanceId          = asSRV(queue, luts.transmittance);
			pushConst.probeTraceLinearIndexSRV = asSRV(queue, probeUpdateLinearIndexBuffer);
			pushConst.scatteringId             = asSRV3DTexture(queue, luts.scatteringTexture);

			// 
			if (luts.optionalSingleMieScatteringTexture != nullptr)
			{
				pushConst.singleMieScatteringId = asSRV3DTexture(queue, luts.optionalSingleMieScatteringTexture);
			}
			pushConst.irradianceTextureId     = asSRV(queue, luts.irradianceTexture);
			pushConst.linearSampler           = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();

			// 
			pushConst.radianceUAV             = asUAV(queue, radianceBuffer);
			pushConst.probeTracedMarkSRV      = asSRV(queue, resource.probeTraceMarkerBuffer);
			pushConst.probeCacheInfoSRV       = asSRV(queue, resource.probeTraceInfoBuffer);
			pushConst.probeCacheRayGbufferSRV = asSRV(queue, resource.probeTraceGbufferInfoBuffer);


			// 
			asSRV(queue, resource.probeTraceHistoryValidBuffer);

			auto computeShader = getContext().getShaderLibrary().getShader<DDGIProbeRelightingCS>();
			addIndirectComputePass2(queue,
				"DDGI: RelightingCS",
				getContext().computePipe(computeShader, "DDGI: Relighting"),
				pushConst,
				relightingCmdBuffer);
		}

		// Irradiance update. 
		{
			auto cmdBuffer = getContext().getBufferPool().createGPUOnly("cmdBuffer", sizeof(uint4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
			{
				DDGIConvolutionPushConsts pushConsts{};
				pushConsts.probeCounterSRV = asSRV(queue, probeTraceCounterBuffer);
				pushConsts.cmdBufferId = asUAV(queue, cmdBuffer);

				auto computeShader = getContext().getShaderLibrary().getShader<DDGIConvolutionIndirectCmdParamCS>();
				addComputePass2(queue,
					"DDGI: ConvolutionIndirectBufferCS",
					getContext().computePipe(computeShader, "DDGI: ConvolutionIndirectBuffer"),
					pushConsts,
					{ 1, 1, 1 });
			}


			DDGIConvolutionPushConsts irradianceConst{};
			irradianceConst.cameraViewId       = cameraViewId;
			irradianceConst.ddgiConfigBufferId = ddgiConfigBufferId;
			irradianceConst.ddgiConfigId       = ddgiVolumeId;
			irradianceConst.UAV                = asUAV(queue, resource.iradianceTexture);

			// 
			irradianceConst.probeTraceLinearIndexSRV = asSRV(queue, probeUpdateLinearIndexBuffer);
			irradianceConst.probeTracedMarkSRV       = asSRV(queue, resource.probeTraceMarkerBuffer);
			irradianceConst.probeCacheRayGbufferSRV  = asSRV(queue, resource.probeTraceGbufferInfoBuffer);
			irradianceConst.probeRadianceSRV         = asSRV(queue, radianceBuffer);
			irradianceConst.probeCacheInfoSRV        = asSRV(queue, resource.probeTraceInfoBuffer);

			// 
			irradianceConst.probeHistoryValidUAV = asUAV(queue, resource.probeTraceHistoryValidBuffer);

			DDGIConvolutionCS::Permutation permutation;
			permutation.set<DDGIConvolutionCS::SV_bIrradiance>(true);
			auto computeShader = getContext().getShaderLibrary().getShader<DDGIConvolutionCS>(permutation);

			addIndirectComputePass2(queue,
				"DDGI: ConvolutionIrradianceCS",
				getContext().computePipe(computeShader, "DDGI: ConvolutionIrradiance"),
				irradianceConst,
				cmdBuffer);

			asSRV(queue, resource.iradianceTexture);
		}

		asSRV(queue, resource.probeTraceHistoryValidBuffer);
	}

	PoolTextureRef ddgiTexture = nullptr; getContext().getTexturePool().create("DDGIApply", gbuffers.dimension.x, gbuffers.dimension.y, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	if (sEnableDDGIDebugOutput > 0)
	{
		ddgiTexture = getContext().getTexturePool().create("DDGIApply", gbuffers.dimension.x, gbuffers.dimension.y, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		const uint2 dispatchDim = divideRoundingUp(gbuffers.dimension, uint2(8));
		DDGIDebugSamplePushConsts pushConst{};

		pushConst.cameraViewId = cameraViewId;
		pushConst.ddgiConfigBufferId = ddgiConfigBufferId;
		pushConst.ddgiCount = kDDGICsacadeCount;
		pushConst.UAV = asUAV(queue, ddgiTexture);
		pushConst.workDim = { gbuffers.dimension.x, gbuffers.dimension.y };
		pushConst.normalRSId = asSRV(queue, gbuffers.pixelRSNormal);
		pushConst.depthTextureId = asSRV(queue, gbuffers.depthStencil, helper::buildDepthImageSubresource());


		auto computeShader = getContext().getShaderLibrary().getShader<DDGIDebugSampleCS>();
		addComputePass2(queue,
			"DDGI: DebugSampleCS",
			getContext().computePipe(computeShader, "DDGI: DebugSample"),
			pushConst,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	return ddgiTexture;
}