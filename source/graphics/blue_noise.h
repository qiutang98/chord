#pragma once

#include <graphics/bufferpool.h>

#define SHADER_DISABLE_NAMESPACE_USING
#include <shader/base.h>

namespace chord::graphics
{
	struct BlueNoiseContext : NonCopyable
	{
		explicit BlueNoiseContext();

		GPUBlueNoiseContext getGPUBlueNoiseCtx() const;

		// STBN texture.
		GPUTextureAssetRef stbn_scalar = nullptr;
		GPUTextureAssetRef stbn_vec2   = nullptr;
		GPUTextureAssetRef stbn_vec3   = nullptr;
	};
}