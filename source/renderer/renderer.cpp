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
			m_perframeCameraView.basicData = getGPUBasicData();
			camera->fillViewUniformParameter(m_perframeCameraView);
		}

		// Now collect perframe camera.
		PerframeCollected perframe { };
		GLTFRenderDescriptor gltfRenderDescritptor{};
		{
			// Collected.
			scene->perviewPerframeCollect(perframe, m_perframeCameraView);

			gltfRenderDescritptor.gltfObjectCount = perframe.gltfPrimitives.size();
			gltfRenderDescritptor.perframeCollect = &perframe;

			if (gltfRenderDescritptor.gltfObjectCount > 0)
			{
				gltfRenderDescritptor.gltfBufferId =
					uploadBufferToGPU(cmd, "GLTFObjectInfo-" + m_name, perframe.gltfPrimitives.data(), gltfRenderDescritptor.gltfObjectCount);
			}
			else
			{
				gltfRenderDescritptor.gltfBufferId = ~0;
			}

			m_perframeCameraView.basicData.GLTFObjectCount = gltfRenderDescritptor.gltfObjectCount;
			m_perframeCameraView.basicData.GLTFObjectBuffer = gltfRenderDescritptor.gltfBufferId;
		}


		const uint32 currentRenderWidth = m_dimensionConfig.getRenderWidth();
		const uint32 currentRenderHeight = m_dimensionConfig.getRenderHeight();

		auto finalOutput = getOutput();

		// Graphics start timeline.
		auto& graphics = cmd.getGraphicsQueue();

		// Allocate view gpu uniform buffer.
		uint32 viewGPUId = uploadBufferToGPU(cmd, "PerViewCamera-" + m_name, &m_perframeCameraView);
		
		// 
		auto gbuffers = allocateGBufferTextures(currentRenderWidth, currentRenderHeight);
		auto frameStartTimeline = graphics.getCurrentTimeline();

		graphics.beginCommand({ frameStartTimeline });
		{
			// Clear all gbuffer textures.
			addClearGbufferPass(graphics, gbuffers);

			// Render GLTF
			gltfBasePassRendering(graphics, gbuffers, viewGPUId, gltfRenderDescritptor);

		}
		auto gbufferPassTimeline = graphics.endCommand();


		graphics.beginCommand({ gbufferPassTimeline} );
		{
			tonemapping(graphics, gbuffers.color, finalOutput);

			graphics.transitionPresent(finalOutput);
		}
		graphics.endCommand();
	}

	GPUBasicData getGPUBasicData()
	{
		GPUBasicData result{};

		const auto& GPUScene = Application::get().getGPUScene();

		// Basic data collect.
		result.frameCounter = getFrameCounter();
		result.frameCounterMod8 = getFrameCounter() % 8;
		
		// GPU scene.
		result.GLTFPrimitiveDataBuffer = GPUScene.getGLTFPrimitiveDataPool().getBindlessSRVId();
		result.GLTFPrimitiveDetailBuffer = GPUScene.getGLTFPrimitiveDetailPool().getBindlessSRVId();

		return result;
	}
}
