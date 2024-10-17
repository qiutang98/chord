#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>

namespace chord
{
	// Scene gbuffer textures, render resolution.
	struct GBufferTextures
	{
		uint2 dimension = { 0, 0 };

		// Visibility: .x = encodeTriangleIdInstanceId(primitiveId, instanceId).
		graphics::PoolTextureRef visibility = nullptr;
		static auto visibilityFormat() { return VK_FORMAT_R32_UINT; }

		// Color: .xyz = Store emissive in base pass, lighting color in lighting pass.
		graphics::PoolTextureRef color = nullptr;
		static auto colorFormat() { return VK_FORMAT_B10G11R11_UFLOAT_PACK32; }

		// 
		graphics::PoolTextureRef depthStencil = nullptr;
		static auto depthStencilFormat() { return VK_FORMAT_D32_SFLOAT_S8_UINT; }

		// GBuffer A: .xyz = store relative worldspace vertex normal.
		//            .w   = free 2bit.
		graphics::PoolTextureRef vertexRSNormal = nullptr;
		static auto vertexRSNormalFormat() { return VK_FORMAT_A2B10G10R10_UNORM_PACK32; }

		// GBuffer B: .xyz = store relative worldspace pixel normal.
		//            .w   = free 2bit.
		graphics::PoolTextureRef pixelRSNormal = nullptr;
		static auto pixelRSNormalFormat() { return VK_FORMAT_A2B10G10R10_UNORM_PACK32; }

		// GBuffer C: .xy = motion vector.
		graphics::PoolTextureRef motionVector = nullptr;
		static auto motionVectorFormat() { return VK_FORMAT_R16G16_SFLOAT; }

		// GBuffer D: .x Material AO.
		//            .y Roughness.
		//            .z Metallic.
		//            .w free 8bit.
		graphics::PoolTextureRef aoRoughnessMetallic = nullptr;
		static auto aoRoughnessMetallicFormat() { return VK_FORMAT_R8G8B8A8_UNORM; }


		graphics::PoolTextureRef depth_Half = nullptr;
		static auto depthHalfFormat() { return VK_FORMAT_R32_SFLOAT; }

		graphics::PoolTextureRef motionVector_Half = nullptr;
		graphics::PoolTextureRef vertexRSNormal_Half = nullptr;
		graphics::PoolTextureRef pixelRSNormal_Half = nullptr;
		graphics::PoolTextureRef roughness_Half = nullptr;
		static auto roughnessHalfFormat() { return VK_FORMAT_R8_UNORM; }

		void generateHalfGbuffer();
	};

	extern GBufferTextures allocateGBufferTextures(uint32 width, uint32 height);
	extern void addClearGbufferPass(graphics::GraphicsQueue& queue, GBufferTextures& textures);
}