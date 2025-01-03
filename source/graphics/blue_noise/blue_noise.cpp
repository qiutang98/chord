#include <graphics/graphics.h>
#include <graphics/blue_noise/blue_noise.h>
#include <graphics/helper.h>
#include <utils/image.h>

namespace chord::graphics
{
#ifdef FULL_BLUE_NOISE_SPP
	namespace blueNoise_256_Spp
	{
		// blue noise sampler 256spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_256spp.cpp"
	}

	namespace blueNoise_128_Spp
	{
		// blue noise sampler 128spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_128spp.cpp"
	}

	namespace blueNoise_64_Spp
	{
		// blue noise sampler 64spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_64spp.cpp"
	}

	namespace blueNoise_32_Spp
	{
		// blue noise sampler 32spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_32spp.cpp"
	}
#endif 

	namespace blueNoise_16_Spp
	{
		// blue noise sampler 16spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_16spp.cpp"
	}

	namespace blueNoise_8_Spp
	{
		// blue noise sampler 8spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_8spp.cpp"
	}

	namespace blueNoise_4_Spp
	{
		// blue noise sampler 4spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_4spp.cpp"
	}

	namespace blueNoise_2_Spp
	{
		// blue noise sampler 2spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_2spp.cpp"
	}

	namespace blueNoise_1_Spp
	{
		// blue noise sampler 1spp.
		#include "samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
	}

	BlueNoiseContext::BlueNoiseContext()
	{
		// Upload STBN
		{
			auto ldrImage = std::make_unique<ImageLdr3D>();
			auto uploadVMACI = helper::buildVMAUploadImageAllocationCI();

			auto imageCI = helper::buildBasicUploadImageCreateInfo(128, 128, VK_FORMAT_R8G8B8A8_UNORM);
			imageCI.extent.depth = 64;
			imageCI.imageType = VK_IMAGE_TYPE_3D;

			{
				check(ldrImage->fillFromFile(64, EImageChannelRemapType::eR, [](uint32 index) { 
					return std::format("resource/texture/stbn/vec1/stbn_scalar_2Dx1Dx1D_128x128x64x1_{}.png", index); }));

				imageCI.format = VK_FORMAT_R8_UNORM;

				auto texture = std::make_shared<GPUTexture>("STBN_scalar", imageCI, uploadVMACI);
				getContext().syncUploadTexture(*texture, ldrImage->getSizeBuffer());

				stbn_scalar = std::make_shared<GPUTextureAsset>(texture);
			}

			{
				check(ldrImage->fillFromFile(64, EImageChannelRemapType::eRG, [](uint32 index) {
					return std::format("resource/texture/stbn/vec2/stbn_vec2_2Dx1D_128x128x64_{}.png", index); }));

				imageCI.format = VK_FORMAT_R8G8_UNORM;

				auto texture = std::make_shared<GPUTexture>("STBN_vec2", imageCI, uploadVMACI);
				getContext().syncUploadTexture(*texture, ldrImage->getSizeBuffer());

				stbn_vec2 = std::make_shared<GPUTextureAsset>(texture);
			}

			{
				check(ldrImage->fillFromFile(64, EImageChannelRemapType::eRGBA, [](uint32 index) {
					return std::format("resource/texture/stbn/vec3/stbn_vec3_2Dx1D_128x128x64_{}.png", index); }));

				imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;

				auto texture = std::make_shared<GPUTexture>("STBN_vec3", imageCI, uploadVMACI);
				getContext().syncUploadTexture(*texture, ldrImage->getSizeBuffer());

				stbn_vec3 = std::make_shared<GPUTextureAsset>(texture);
			}
		}


		auto buildBuffer = [](const char* name, void* ptr, VkDeviceSize size)
		{
			return getContext().getBufferPool().createHostVisible(
				name,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				SizedBuffer(size, ptr));
		};

		#define WORK_CODE \
		{\
			BlueNoiseWorkingBuffer.sobol = buildBuffer(BlueNoiseWorkingName, (void*)BlueNoiseWorkingSpace::sobol_256spp_256d, sizeof(BlueNoiseWorkingSpace::sobol_256spp_256d));\
			BlueNoiseWorkingBuffer.rankingTile = buildBuffer(BlueNoiseWorkingName, (void*)BlueNoiseWorkingSpace::rankingTile, sizeof(BlueNoiseWorkingSpace::rankingTile)); \
			BlueNoiseWorkingBuffer.scramblingTile = buildBuffer(BlueNoiseWorkingName, (void*)BlueNoiseWorkingSpace::scramblingTile, sizeof(BlueNoiseWorkingSpace::scramblingTile)); \
		}

		#define BlueNoiseWorkingBuffer spp_1_buffer
		#define BlueNoiseWorkingSpace blueNoise_1_Spp
		#define BlueNoiseWorkingName "Sobel_1_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_2_buffer
		#define BlueNoiseWorkingSpace blueNoise_2_Spp
		#define BlueNoiseWorkingName "Sobel_2_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_4_buffer
		#define BlueNoiseWorkingSpace blueNoise_4_Spp
		#define BlueNoiseWorkingName "Sobel_4_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_8_buffer
		#define BlueNoiseWorkingSpace blueNoise_8_Spp
		#define BlueNoiseWorkingName "Sobel_8_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_16_buffer
		#define BlueNoiseWorkingSpace blueNoise_16_Spp
		#define BlueNoiseWorkingName "Sobel_16_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

	#ifdef FULL_BLUE_NOISE_SPP

		#define BlueNoiseWorkingBuffer spp_32_buffer
		#define BlueNoiseWorkingSpace blueNoise_32_Spp
		#define BlueNoiseWorkingName "Sobel_32_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_64_buffer
		#define BlueNoiseWorkingSpace blueNoise_64_Spp
		#define BlueNoiseWorkingName "Sobel_64_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_128_buffer
		#define BlueNoiseWorkingSpace blueNoise_128_Spp
		#define BlueNoiseWorkingName "Sobel_128_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName

		#define BlueNoiseWorkingBuffer spp_256_buffer
		#define BlueNoiseWorkingSpace blueNoise_256_Spp
		#define BlueNoiseWorkingName "Sobel_256_spp_buffer"
			WORK_CODE
		#undef BlueNoiseWorkingBuffer
		#undef BlueNoiseWorkingSpace 
		#undef BlueNoiseWorkingName
	#endif

		#undef WORK_CODE
	}

	GPUBlueNoiseContext BlueNoiseContext::getGPUBlueNoiseCtx() const
	{
		GPUBlueNoiseContext result { };

		result.spp_1 = spp_1_buffer.getGPUBlueNoise();
		result.spp_2 = spp_2_buffer.getGPUBlueNoise();
		result.spp_4 = spp_4_buffer.getGPUBlueNoise();
		result.spp_8 = spp_8_buffer.getGPUBlueNoise();
		result.spp_16 = spp_16_buffer.getGPUBlueNoise();
	#ifdef FULL_BLUE_NOISE_SPP
		result.spp_32 = spp_32_buffer.getGPUBlueNoise();
		result.spp_64 = spp_64_buffer.getGPUBlueNoise();
		result.spp_128 = spp_128_buffer.getGPUBlueNoise();
		result.spp_256 = spp_256_buffer.getGPUBlueNoise();
	#endif

		result.STBN_scalar = stbn_scalar->getSRV(graphics::helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_3D);
		result.STBN_vec2   =   stbn_vec2->getSRV(graphics::helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_3D);
		result.STBN_vec3   =   stbn_vec3->getSRV(graphics::helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_3D);

		return result;
	}

	GPUBlueNoise BlueNoiseContext::BufferMisc::getGPUBlueNoise() const
	{
		GPUBlueNoise result { };

		result.sobol = sobol->getBindlessIndex();
		result.rankingTile = rankingTile->getBindlessIndex();
		result.scramblingTile = scramblingTile->getBindlessIndex();

		return result;
	}
}