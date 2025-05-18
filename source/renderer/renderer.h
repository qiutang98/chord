#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <graphics/graphics.h>
#include <graphics/texture_pool.h>
#include <shader/base.h>
#include <utils/camera.h>
#include <graphics/buffer_pool.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/atmosphere.h>
#include <scene/manager/shadow.h>

namespace chord
{
	extern GPUBasicData getGPUBasicData(const AtmosphereParameters& atmosphere);

	template<typename T>
	static inline std::pair<graphics::PoolBufferHostVisible, uint32> uploadBufferToGPU(graphics::CommandList& cmd, const std::string& name, const T* data, uint32 count = 1)
	{
		ZoneScoped;

		using namespace graphics;

		VkBufferUsageFlags usageFlag = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

		auto newGPUBuffer = getContext().getBufferPool().createHostVisibleCopyUpload(
			name,
			usageFlag,
			SizedBuffer(sizeof(T) * count, (void*)data));

		// Insert perframe lazy destroy.
		cmd.addReferenceResource(newGPUBuffer);
		const auto& viewC = newGPUBuffer->get().requireView(true, false);
		return { newGPUBuffer, viewC.storage.get() };
	}

	class DeferredRenderer : NonCopyable
	{
	public:
		explicit DeferredRenderer(const std::string& name);
		virtual ~DeferredRenderer();

		graphics::PoolTextureRef getOutput();

		void render(
			const ApplicationTickData& tickData, 
			graphics::CommandList& cmd,
			ICamera* camera);

	public:
		static constexpr auto kMinRenderDim = 64U;
		static constexpr auto kMaxRenderDim = 4096U;

		class DimensionConfig
		{
		public:
			DimensionConfig();

			uint32 getRenderWidth()  const { return m_renderDim.x; }
			uint32 getRenderHeight() const { return m_renderDim.y; }

			uint32 getPostWidth()    const { return m_postDim.x; }
			uint32 getPostHeight()   const { return m_postDim.y; }

			uint32 getOutputWidth()  const { return m_outputDim.x; }
			uint32 getOutputHeight() const { return m_outputDim.y; }

			bool updateDimension(
				uint32 outputWidth,
				uint32 outputHeight,
				float renderScaleToPost,
				float postScaleToOutput);

			auto operator<=>(const DimensionConfig&) const = default;

		private:
			// Dimension which render depth, gbuffer, lighting and pre-post effect.
			math::uvec2 m_renderDim;

			// Dimension which render Upscale, post-effect.
			math::uvec2 m_postDim;

			// Dimension which do final upscale to the viewport.
			math::uvec2 m_outputDim;


			float m_renderScaleToPost;
		};

		bool updateDimension(uint32 outputWidth, uint32 outputHeight, float renderScaleToPost, float postScaleToOutput);
		const auto& getTimingValues() { return m_timeStamps; }

	protected:
		void clearHistory(bool bClearOutput);

	protected:
		std::string m_name;

		// Render image dimension config.
		DimensionConfig m_dimensionConfig = {};

		// Renderer output image.
		graphics::PoolTextureRef m_outputTexture = nullptr;

		// Renderer history.
		DeferredRendererHistory m_rendererHistory;

		// Current frame camera relative view uniform buffer.
		PerframeCameraView m_perframeCameraView;

		// GPU timer.
		graphics::GPUTimestamps m_rendererTimer;
		std::vector<graphics::GPUTimestamps::TimeStamp> m_timeStamps;

		// 
		bool m_bTLASValidCurrentFrame = false;
		graphics::helper::TLASBuilder m_tlas;

		DDGIContext m_ddgiCtx;
		GIContext m_giCtx;

		bool m_bCameraCut = false;
	};
}