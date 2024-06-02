#include <graphics/graphics.h>
#include <renderer/renderer.h>
#include <graphics/helper.h>

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

			m_outputTexture = getContext().getTexturePool().create(name,
				m_dimensionConfig.getOutputWidth(),
				m_dimensionConfig.getOutputHeight(),
				VK_FORMAT_A2B10G10R10_UNORM_PACK32, // Default Render to R10G10B10A2 Image.
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

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

	void DeferredRenderer::render(const ApplicationTickData& tickData, graphics::CommandList& cmd)
	{
		LOG_INFO("Hellp");
	}
}
