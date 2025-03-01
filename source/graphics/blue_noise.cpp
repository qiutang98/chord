#include <graphics/graphics.h>
#include <graphics/blue_noise.h>
#include <graphics/helper.h>
#include <utils/image.h>

namespace chord::graphics
{
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
	}

	GPUBlueNoiseContext BlueNoiseContext::getGPUBlueNoiseCtx() const
	{
		GPUBlueNoiseContext result { };

		result.STBN_scalar = stbn_scalar->getSRV(graphics::helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_3D);
		result.STBN_vec2   =   stbn_vec2->getSRV(graphics::helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_3D);
		result.STBN_vec3   =   stbn_vec3->getSRV(graphics::helper::buildBasicImageSubresource(), VK_IMAGE_VIEW_TYPE_3D);

		return result;
	}
}