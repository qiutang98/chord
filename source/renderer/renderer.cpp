#include <graphics/graphics.h>
#include <renderer/renderer.h>
#include <graphics/helper.h>

#include <renderer/render_textures.h>
#include <renderer/fullscreen.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/base.h>
#include <renderer/mesh/gltf_rendering.h>
#include <application/application.h>
#include <renderer/gpu_scene.h>
#include <scene/scene_manager.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/visibility_tile.h>
#include <scene/component/sky.h>
#include <scene/scene.h>
#include <renderer/lighting.h>

using namespace chord;
using namespace chord::graphics;
constexpr uint32 kTimerFramePeriod = 3;
constexpr uint32 kTimerStampMaxCount = 128;



DeferredRenderer::DeferredRenderer(const std::string& name)
	: m_name(name)
{
	// Init gpu timer.
	m_rendererTimer.init(kTimerFramePeriod, kTimerStampMaxCount);

	// 
}

DeferredRenderer::~DeferredRenderer()
{
	graphics::getContext().waitDeviceIdle();
	m_rendererTimer.release();
}

PoolTextureRef DeferredRenderer::getOutput()
{
	if (!m_outputTexture)
	{
		const std::string name = std::format("{}-Output", m_name);

		// NOTE: R11G11B10 break quantity of blue channel, exist some banding after present.
		//       R10G10B10A2 Image is best choice.
		m_outputTexture = getContext().getTexturePool().create(name,
			m_dimensionConfig.getOutputWidth(),
			m_dimensionConfig.getOutputHeight(),
			VK_FORMAT_A2R10G10B10_UNORM_PACK32,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

		m_outputTexture->get().transitionImmediately(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, helper::buildBasicImageSubresource());
	}

	return m_outputTexture;
}

bool DeferredRenderer::updateDimension(
	uint32 outputWidth,
	uint32 outputHeight,
	float renderScaleToPost,
	float postScaleToOutput)
{
	bool bChange = m_dimensionConfig.updateDimension(outputWidth, outputHeight, renderScaleToPost, postScaleToOutput);
	if (bChange)
	{
		clearHistory(true);
	}

	return bChange;
}

void DeferredRenderer::clearHistory(bool bClearOutput)
{
	m_rendererHistory = {};

	if (bClearOutput)
	{
		m_outputTexture = nullptr;
	}
}

bool DeferredRenderer::DimensionConfig::updateDimension(uint32 outputWidth, uint32 outputHeight, float renderScaleToPost, float postScaleToOutput)
{
	check(renderScaleToPost > 0.0 && postScaleToOutput > 0.0);

	auto makeDimSafe = [](math::uvec2& in)
	{
		in = math::clamp(in, { kMinRenderDim, kMinRenderDim }, { kMaxRenderDim, kMaxRenderDim });
	};

	DimensionConfig config{ };
	config.m_outputDim = { outputWidth, outputHeight };
	config.m_postDim = math::ceil(math::vec2(config.m_outputDim) / postScaleToOutput);
	config.m_renderDim = math::ceil(math::vec2(config.m_postDim) / renderScaleToPost);

	makeDimSafe(config.m_outputDim);
	makeDimSafe(config.m_postDim);
	makeDimSafe(config.m_renderDim);

	bool bChange = (config != *this);
	if (bChange)
	{
		*this = config;
	}

	return bChange;
}

DeferredRenderer::DimensionConfig::DimensionConfig()
	: m_renderDim({ kMinRenderDim, kMinRenderDim })
	, m_postDim({ kMinRenderDim, kMinRenderDim })
	, m_outputDim({ kMinRenderDim, kMinRenderDim })
{

}

void DeferredRenderer::render(
	const ApplicationTickData& tickData, 
	graphics::CommandList& cmd,
	ICamera* camera)
{
	const uint32 currentRenderWidth = m_dimensionConfig.getRenderWidth();
	const uint32 currentRenderHeight = m_dimensionConfig.getRenderHeight();

	auto* sceneManager = &Application::get().getEngine().getSubsystem<SceneManager>();
	auto scene = sceneManager->getActiveScene();

	// Update atmosphere parameters.
	auto atmosphereParam = scene->getAtmosphereManager().update(tickData);

	// Graphics start timeline.
	auto& graphics = cmd.getGraphicsQueue();
	auto frameStartTimeline = graphics.getCurrentTimeline();

	// Render 
	graphics.beginCommand({ frameStartTimeline });

	// GPU timer start.
	m_rendererTimer.onBeginFrame(graphics.getActiveCmd()->commandBuffer, &m_timeStamps);

	// Update camera info first.
	{
		auto lastFrame = m_perframeCameraView;
		auto& currentFrame = m_perframeCameraView;

		currentFrame.basicData = getGPUBasicData(atmosphereParam);
		camera->fillViewUniformParameter(currentFrame);

		currentFrame.renderDimension = {
			1.0f * float(currentRenderWidth),
			1.0f * float(currentRenderHeight),
			1.0f / float(currentRenderWidth),
			1.0f / float(currentRenderHeight)
		};

		// Update last frame infos.
		currentFrame.translatedWorldToClipLastFrame = lastFrame.translatedWorldToClip;
		currentFrame.translatedWorldToClipLastFrame_NoJitter = lastFrame.translatedWorldToClip_NoJitter;
		currentFrame.clipToTranslatedWorld_LastFrame = lastFrame.clipToTranslatedWorld;
		
		currentFrame.cameraWorldPosLastFrame = lastFrame.cameraWorldPos;
		for (uint i = 0; i < 6; i++) { currentFrame.frustumPlaneLastFrame[i] = lastFrame.frustumPlane[i]; }
	}

	DebugLineCtx debugLineCtx = allocateDebugLineCtx();
	auto finalOutput = getOutput();

	// Now collect perframe camera.
	PerframeCollected perframe { };
	uint gltfBufferId = ~0;
	uint gltfObjectCount = 0;
	uint32 viewGPUId;
	uint32 mainViewInstanceCullingInfoId;
	{
		perframe.debugLineCtx = &debugLineCtx;

		// Collected.
		scene->perviewPerframeCollect(perframe, m_perframeCameraView, camera);

		// Update gltf object count.
		gltfObjectCount = perframe.gltfPrimitives.size();

		uint gltfBufferId = ~0;
		if (gltfObjectCount > 0)
		{
			gltfBufferId =
				uploadBufferToGPU(cmd, "GLTFObjectInfo-" + m_name, perframe.gltfPrimitives.data(), gltfObjectCount).second;
		}

		// GLTF object.
		{
			m_perframeCameraView.basicData.GLTFObjectCount  = gltfObjectCount;
			m_perframeCameraView.basicData.GLTFObjectBuffer = gltfBufferId;
		}

		// debugline ctx.
		{
			m_perframeCameraView.basicData.debuglineMaxCount = debugLineCtx.gpuMaxCount;
			m_perframeCameraView.basicData.debuglineCount    = debugLineCtx.gpuCountBuffer->get().requireView(true, false).storage.get();
			m_perframeCameraView.basicData.debuglineVertices = debugLineCtx.gpuVertices->get().requireView(true, false).storage.get();
		}

		// Allocate view gpu uniform buffer.
		viewGPUId = uploadBufferToGPU(cmd, "PerViewCamera-" + m_name, &m_perframeCameraView).second;

		// update debugline gpu.
		debugLineCtx.cameraViewBufferId = viewGPUId;

		{
			InstanceCullingViewInfo mainViewInstanceCullingInfo{ };
			for (uint i = 0; i < 6; i++)
			{
				mainViewInstanceCullingInfo.frustumPlanesRS[i] = m_perframeCameraView.frustumPlane[i];
			}
			mainViewInstanceCullingInfo.translatedWorldToClip = m_perframeCameraView.translatedWorldToClip;
			mainViewInstanceCullingInfo.clipToTranslatedWorld = math::inverse(mainViewInstanceCullingInfo.translatedWorldToClip);
			mainViewInstanceCullingInfo.cameraWorldPos = m_perframeCameraView.cameraWorldPos;
			mainViewInstanceCullingInfo.renderDimension = m_perframeCameraView.renderDimension;

			mainViewInstanceCullingInfoId = uploadBufferToGPU(cmd, "MainViewInstanceCullingInfo" + m_name, &mainViewInstanceCullingInfo).second;
		}
	}

	// Cascade shadow for sun
	const float3 sunDirection = float3(m_perframeCameraView.basicData.sunInfo.direction);
	
	// 
	const auto& sunShadowConfig = scene->getShadowManager().getConfig();

	// 
	auto gbuffers = allocateGBufferTextures(currentRenderWidth, currentRenderHeight);

	auto insertTimer = [&](const std::string& label, GraphicsOrComputeQueue& queue)
	{
		m_rendererTimer.getTimeStamp(queue.getActiveCmd()->commandBuffer, label.c_str());
	};

	GLTFRenderContext gltfRenderCtx(&perframe, gltfObjectCount, gltfBufferId, viewGPUId);
	gltfRenderCtx.timerLambda = [&](const std::string& label, GraphicsOrComputeQueue& queue) { insertTimer(label, queue); };

	debugLineCtx.prepareForRender(graphics);

	HZBContext hzbCtx { };
	CascadeShadowHistory cascadeShadowCurrentFrame{ };
	{
		insertTimer("FrameBegin", graphics);

		m_bTLASValidCurrentFrame = perframe.asInstances.isExistInstance();
		if (m_bTLASValidCurrentFrame)
		{
			m_tlas.buildTlas(graphics, perframe.asInstances.asInstances, false, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
			insertTimer("TLAS", graphics);
		}

		const AtmosphereLut& atmosphereLuts = scene->getAtmosphereManager().render(tickData, cmd, graphics);

		// Clear all gbuffer textures.
		addClearGbufferPass(graphics, gbuffers);
		insertTimer("Clear GBuffers", graphics);

		CountAndCmdBuffer postInstanceCullingBuffer = {};
		if (shouldRenderGLTF(gltfRenderCtx))
		{
			postInstanceCullingBuffer = instanceCulling(graphics, gltfRenderCtx, mainViewInstanceCullingInfoId, 0);
			insertTimer("GLTF Instance Culling", graphics);

			// Prepass stage0
			CountAndCmdBuffer stageCountAndCmdBuffer = {};
			const bool bShouldStage1GLTF = gltfVisibilityRenderingStage0(graphics, gbuffers, gltfRenderCtx, m_rendererHistory.hzbCtx, mainViewInstanceCullingInfoId, 0, postInstanceCullingBuffer, stageCountAndCmdBuffer);
			insertTimer("GLTF Visibility Stage0", graphics);

			// Prepass stage1
			if (bShouldStage1GLTF)
			{
				check(stageCountAndCmdBuffer.first != nullptr && stageCountAndCmdBuffer.second != nullptr);

				auto tempHzbCtx = buildHZB(graphics, gbuffers.depthStencil, true, false, false);
				insertTimer("BuildHZB Post Prepass Stage0", graphics);

				gltfVisibilityRenderingStage1(graphics, gbuffers, gltfRenderCtx, tempHzbCtx, mainViewInstanceCullingInfoId, 0, stageCountAndCmdBuffer);
				insertTimer("GLTF Visibility Stage1", graphics);
			}
		}

		{
			hzbCtx = buildHZB(graphics, gbuffers.depthStencil, true, true, true);
			insertTimer("BuildHZB", graphics);
		}

		// Shadow depth rendering.
		CascadeShadowContext cascadeContext { };
		if (shouldRenderGLTF(gltfRenderCtx))
		{
			cascadeContext = chord::renderShadow(cmd, graphics, gltfRenderCtx, m_perframeCameraView, m_rendererHistory.cascadeCtx, sunShadowConfig, tickData, *camera, sunDirection, hzbCtx);
			cascadeShadowCurrentFrame = chord::extractCascadeShadowHistory(graphics, sunDirection, cascadeContext, tickData);
		}

		auto mainViewCulledCmdBuffer = postInstanceCullingBuffer.second;

		PoolTextureRef disocclusionMask = nullptr;


		VisibilityTileMarkerContext visibilityCtx;
		if (shouldRenderGLTF(gltfRenderCtx))
		{
			visibilityCtx = visibilityMark(graphics, viewGPUId, mainViewCulledCmdBuffer, gbuffers.visibility);
			insertTimer("Visibility Tile Marker", graphics);

			lighting(graphics, gbuffers, viewGPUId, mainViewCulledCmdBuffer, visibilityCtx);
			insertTimer("lighting Tile", graphics);

			if (m_rendererHistory.depth_Half != nullptr && m_rendererHistory.vertexNormalRS_Half != nullptr)
			{
				disocclusionMask = computeDisocclusionMask(graphics, gbuffers, viewGPUId, m_rendererHistory.depth_Half, m_rendererHistory.vertexNormalRS_Half);
				insertTimer("DisocclusionMask", graphics);
			}


			auto cascadeResult = cascadeShadowEvaluate(graphics, gbuffers, viewGPUId, cascadeContext, m_rendererHistory.cascadeCtx.softShadowMask, disocclusionMask);
			cascadeShadowCurrentFrame.softShadowMask = cascadeResult.softShadowMask;
			insertTimer("PCSS", graphics);
		}

		renderSky(graphics, gbuffers.color, gbuffers.depthStencil, viewGPUId, atmosphereLuts);

		// Visualize for nanite.
		if (shouldRenderGLTF(gltfRenderCtx))
		{
			visualizeNanite(graphics, gbuffers, viewGPUId, mainViewCulledCmdBuffer, visibilityCtx);
			insertTimer("Nanite visualize", graphics);
		}


		check(finalOutput->get().getExtent().width == gbuffers.color->get().getExtent().width);
		check(finalOutput->get().getExtent().height == gbuffers.color->get().getExtent().height);
		tonemapping(viewGPUId, graphics, gbuffers.color, finalOutput);
		insertTimer("Tonemapping", graphics);

		check(finalOutput->get().getExtent().width == gbuffers.depthStencil->get().getExtent().width);
		check(finalOutput->get().getExtent().height == gbuffers.depthStencil->get().getExtent().height);
		debugLine(graphics, debugLineCtx, gbuffers.depthStencil, finalOutput);
		insertTimer("DebugLine", graphics);


		graphics.transitionPresent(finalOutput);
		insertTimer("FrameEnd", graphics);
	}
	m_rendererTimer.onEndFrame();
	graphics.endCommand();

	// Update history. 
	{
		m_rendererHistory.hzbCtx = hzbCtx;
		m_rendererHistory.cascadeCtx = cascadeShadowCurrentFrame;

		m_rendererHistory.depth_Half = gbuffers.depth_Half;
		m_rendererHistory.vertexNormalRS_Half = gbuffers.vertexRSNormal_Half;
	}
}

GPUBasicData chord::getGPUBasicData(const AtmosphereParameters& atmosphere)
{
	GPUBasicData result { };

	result.blueNoiseCtx = getContext().getBlueNoise().getGPUBlueNoiseCtx();

	// Fill atmosphere first.
	result.atmosphere = atmosphere;

	auto* sceneManager = &Application::get().getEngine().getSubsystem<SceneManager>();
	auto  scene = sceneManager->getActiveScene();
	const auto& GPUScene = Application::get().getGPUScene();

	if (auto skyComp = scene->getFirstComponent<SkyComponent>())
	{
		result.sunInfo = skyComp->getSkyLightInfo();
	}
	else
	{
		result.sunInfo = getDefaultSkyLightInfo();
	}


	// Basic data collect.
	result.frameCounter = getFrameCounter() % uint64(UINT32_MAX);
	result.frameCounterMod8 = getFrameCounter() % 8;
		
	// GPU scene.
	GPUScene.fillGPUBasicData(result);


	result.pointClampEdgeSampler = getContext().getSamplerManager().pointClampEdge().index.get();
	result.linearClampEdgeSampler = getContext().getSamplerManager().linearClampEdge().index.get();
	return result;
}
