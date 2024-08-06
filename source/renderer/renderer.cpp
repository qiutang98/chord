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

namespace chord
{
	using namespace graphics;

	DeferredRenderer::DeferredRenderer(const std::string& name)
		: m_name(name)
	{
		
	}

	DeferredRenderer::~DeferredRenderer()
	{
		graphics::getContext().waitDeviceIdle();
	}

	PoolTextureRef DeferredRenderer::getOutput()
	{
		if (!m_outputTexture)
		{
			const std::string name = std::format("{}-Output", m_name);

			// NOTE: R11G11B10 break quantity of blue channel, exist some banding after present.
			//       R10G10B10A2 Image is best choice, but we need draw some translucent ui on it.
			//       Just use R8G8B8A8 srgb, fine for most case.
			m_outputTexture = getContext().getTexturePool().create(name,
				m_dimensionConfig.getOutputWidth(),
				m_dimensionConfig.getOutputHeight(),
				VK_FORMAT_R8G8B8A8_SRGB,
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

	template<typename T>
	uint32 uploadBufferToGPU(CommandList& cmd, const std::string& name, const T* data, uint32 count = 1)
	{
		VkBufferUsageFlags usageFlag = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

		auto newGPUBuffer = getContext().getBufferPool().createHostVisible(
			name,
			usageFlag,
			SizedBuffer(sizeof(T) * count, (void*)data));

		// Insert perframe lazy destroy.
		cmd.insertPendingResource(newGPUBuffer);
		const auto& viewC = newGPUBuffer->get().requireView(true, false);
		return viewC.storage.get();
	}

	void DeferredRenderer::render(
		const ApplicationTickData& tickData, 
		graphics::CommandList& cmd,
		ICamera* camera)
	{
		auto* sceneManager = &Application::get().getEngine().getSubsystem<SceneManager>();
		auto scene = sceneManager->getActiveScene();

		// Update camera info first.
		{
			auto lastFrame = m_perframeCameraView;
			auto& currentFrame = m_perframeCameraView;

			currentFrame.basicData = getGPUBasicData();
			camera->fillViewUniformParameter(currentFrame);

			// Update last frame infos.
			currentFrame.translatedWorldToClipLastFrame = lastFrame.translatedWorldToClip;
			for (uint i = 0; i < 6; i++) { currentFrame.frustumPlaneLastFrame[i] = lastFrame.frustumPlane[i]; }
		}

		DebugLineCtx debugLineCtx = allocateDebugLineCtx();

		// Now collect perframe camera.
		PerframeCollected perframe { };
		uint gltfBufferId = ~0;
		uint gltfObjectCount = 0;
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
					uploadBufferToGPU(cmd, "GLTFObjectInfo-" + m_name, perframe.gltfPrimitives.data(), gltfObjectCount);
			}

			// GLTF object.
			{
				m_perframeCameraView.basicData.GLTFObjectCount = gltfObjectCount;
				m_perframeCameraView.basicData.GLTFObjectBuffer = gltfBufferId;
			}

			// debugline ctx.
			{
				m_perframeCameraView.basicData.debuglineMaxCount = debugLineCtx.gpuMaxCount;
				m_perframeCameraView.basicData.debuglineCount = debugLineCtx.gpuCountBuffer->get().requireView(true, false).storage.get();
				m_perframeCameraView.basicData.debuglineVertices = debugLineCtx.gpuVertices->get().requireView(true, false).storage.get();
			}
		}

		const uint32 currentRenderWidth = m_dimensionConfig.getRenderWidth();
		const uint32 currentRenderHeight = m_dimensionConfig.getRenderHeight();

		auto finalOutput = getOutput();

		// Graphics start timeline.
		auto& graphics = cmd.getGraphicsQueue();

		// Allocate view gpu uniform buffer.
		uint32 viewGPUId = uploadBufferToGPU(cmd, "PerViewCamera-" + m_name, &m_perframeCameraView);

		// update debugline gpu.
		{
			debugLineCtx.cameraViewBufferId = viewGPUId;
		}


		
		// 
		auto gbuffers = allocateGBufferTextures(currentRenderWidth, currentRenderHeight);

		GLTFRenderContext gltfRenderCtx(
			&perframe,
			gltfObjectCount,
			gltfBufferId,
			viewGPUId,
			graphics,
			gbuffers,
			m_rendererHistory);

		auto frameStartTimeline = graphics.getCurrentTimeline();

		HZBContext hzbCtx { };
		graphics.beginCommand({ frameStartTimeline });
		{
			debugLineCtx.prepareForRender(graphics);

			// Clear all gbuffer textures.
			addClearGbufferPass(graphics, gbuffers);


			gltfPrePassRendering(gltfRenderCtx);

			hzbCtx = buildHZB(graphics, gbuffers.depthStencil);

			// Render GLTF
			gltfBasePassRendering(gltfRenderCtx, hzbCtx);

			check(finalOutput->get().getExtent().width == gbuffers.color->get().getExtent().width);
			check(finalOutput->get().getExtent().height == gbuffers.color->get().getExtent().height);
			tonemapping(graphics, gbuffers.color, finalOutput);

			check(finalOutput->get().getExtent().width == gbuffers.depthStencil->get().getExtent().width);
			check(finalOutput->get().getExtent().height == gbuffers.depthStencil->get().getExtent().height);
			debugLine(graphics, debugLineCtx, gbuffers.depthStencil, finalOutput);

			graphics.transitionPresent(finalOutput);
		}
		auto gbufferPassTimeline = graphics.endCommand();


		m_rendererHistory.hzbCtx = hzbCtx;
	}

	GPUBasicData getGPUBasicData()
	{
		GPUBasicData result{};

		const auto& GPUScene = Application::get().getGPUScene();

		// Basic data collect.
		result.frameCounter = getFrameCounter() % uint64(UINT32_MAX);
		result.frameCounterMod8 = getFrameCounter() % 8;
		
		// GPU scene.
		GPUScene.fillGPUBasicData(result);


		result.pointClampEdgeSampler = getContext().getSamplerManager().pointClampEdge().index.get();

		return result;
	}
}
