#include <graphics/graphics.h>
#include <graphics/blue_noise/blue_noise.h>

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