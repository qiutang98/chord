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

			m_outputTexture->get()->transitionImmediately(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, helper::buildBasicImageSubresource());
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

	PoolBufferHostVisible DeferredRenderer::getCameraViewUniformBuffer(
		uint32& outId,
		const ApplicationTickData& tickData, 
		CommandList& cmd,
		ICamera* camera)
	{
		m_perframeCameraView.basicData = getGPUBasicData();


		camera->fillViewUniformParameter(m_perframeCameraView);
		auto perframeGPU = getContext().getBufferPool().createHostVisible(
			"PerframeCameraView - " + m_name,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			SizedBuffer(sizeof(m_perframeCameraView), (void*)&m_perframeCameraView));

		// Insert perframe lazy destroy.
		cmd.insertPendingResource(perframeGPU);

		// Require buffer.
		outId = perframeGPU->get().requireView(false, true).uniform.get();

		return perframeGPU;
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

		auto finalOutput = getOutput();

		// Graphics start timeline.
		auto& graphics = cmd.getGraphicsQueue();

		// Allocate view gpu uniform buffer.
		uint32 viewGPUId;
		auto viewGPU = getCameraViewUniformBuffer(viewGPUId, tickData, cmd, camera);
		
		// 
		auto gbuffers = allocateGBufferTextures(currentRenderWidth, currentRenderHeight);
		auto frameStartTimeline = graphics.getCurrentTimeline();

		graphics.beginCommand({ frameStartTimeline });
		{
			// Clear all gbuffer textures.
			addClearGbufferPass(graphics, gbuffers);

			// Render GLTF
			gltfBasePassRendering(graphics, gbuffers, viewGPUId);

		}
		auto gbufferPassTimeline = graphics.endCommand();


		graphics.beginCommand({ gbufferPassTimeline} );
		{
			tonemapping(graphics, gbuffers.color, finalOutput);

			graphics.transitionPresent(finalOutput->get());
		}
		graphics.endCommand();
	}

	GPUBasicData getGPUBasicData()
	{
		GPUBasicData result{};

		const auto& GPUScene = Application::get().getGPUScene();

		result.frameCounter = getFrameCounter();
		result.frameCounterMod8 = getFrameCounter() % 8;
		
		result.GLTFPrimitiveDataBuffer = GPUScene.getGLTFPrimitiveDataPool().getBindlessSRVId();
		result.GLTFPrimitiveDetailBuffer = GPUScene.getGLTFPrimitiveDetailPool().getBindlessSRVId();

		return result;
	}
}
