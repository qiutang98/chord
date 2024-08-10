#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>

namespace chord
{
	// Scene gbuffer textures, render resolution.
	struct GBufferTextures
	{
		// Visibility: .x = encodeObjectInfo(ShadingType, ObjectId)
		//             .y = triangle id.
		graphics::PoolTextureRef visibility = nullptr;
		static auto visibilityFormat() { return VK_FORMAT_R32G32_UINT; }

		// Color: .xyz = Store emissive in base pass, lighting color in lighting pass.
		// 
		graphics::PoolTextureRef color = nullptr;
		static auto colorFormat() { return VK_FORMAT_B10G11R11_UFLOAT_PACK32; }

		// 
		graphics::PoolTextureRef depthStencil = nullptr;
		static auto depthStencilFormat() { return VK_FORMAT_D32_SFLOAT_S8_UINT; }

		// GBuffer A: .xyz = store worldspace normal.
		//            .w   = free 2bit.
		graphics::PoolTextureRef gbufferA = nullptr;
		static auto gbufferAFormat() { return VK_FORMAT_A2B10G10R10_UNORM_PACK32; }

		// GBuffer B: .x = metallic
		//            .y = roughness
		//            .z = free 8bit
		//            .w = free 8bit
		graphics::PoolTextureRef gbufferB = nullptr;
		static auto gbufferBFormat() { return VK_FORMAT_R8G8B8A8_UNORM; }


		// Gbuffer C: .xyz = rec709 gamma encoded base color.
		//          : .w   = material AO.
		graphics::PoolTextureRef gbufferC = nullptr;
		static auto gbufferCFormat() { return VK_FORMAT_B8G8R8A8_SRGB; }
	};

	extern GBufferTextures allocateGBufferTextures(uint32 width, uint32 height);
	extern void addClearGbufferPass(graphics::GraphicsQueue& queue, GBufferTextures& textures);
}