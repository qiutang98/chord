#pragma once

#include <graphics/bufferpool.h>

#define SHADER_DISABLE_NAMESPACE_USING
#include <shader/base.h>

namespace chord::graphics
{
	struct BlueNoiseContext : NonCopyable
	{
		struct BufferMisc
		{
			PoolBufferHostVisible sobol          = nullptr;
			PoolBufferHostVisible rankingTile    = nullptr;
			PoolBufferHostVisible scramblingTile = nullptr;

			GPUBlueNoise getGPUBlueNoise() const;
		};

		BufferMisc spp_1_buffer;
		BufferMisc spp_2_buffer;
		BufferMisc spp_4_buffer;
		BufferMisc spp_8_buffer;
		BufferMisc spp_16_buffer;
	#ifdef FULL_BLUE_NOISE_SPP
		BufferMisc spp_32_buffer;
		BufferMisc spp_64_buffer;
		BufferMisc spp_128_buffer;
		BufferMisc spp_256_buffer;
	#endif 
		explicit BlueNoiseContext();

		GPUBlueNoiseContext getGPUBlueNoiseCtx() const;

		// STBN texture.
		GPUTextureAssetRef stbn_scalar = nullptr;
		GPUTextureAssetRef stbn_vec2   = nullptr;
		GPUTextureAssetRef stbn_vec3   = nullptr;
	};
}