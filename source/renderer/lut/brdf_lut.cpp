#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>

#include <shader/brdf_lut.hlsl>
#include <renderer/lighting.h>
#include <asset/serialize.h>

namespace chord
{

	static bool sEnableCacheBRDFLut = 0;
	static AutoCVarRef cVarEnableCacheBRDFLut(
		"r.lut.brdf_lut.cache",
		sEnableCacheBRDFLut,
		"Enable cache brdf lut or not."
	);

	namespace graphics
	{
		PRIVATE_GLOBAL_SHADER(BRDFLutCS, "resource/shader/brdf_lut.hlsl", "mainCS", EShaderStage::Compute);
	}


	graphics::PoolTextureRef chord::computeBRDFLut(graphics::GraphicsOrComputeQueue& queue, uint lutDim)
	{
		using namespace graphics;

		const std::filesystem::path brdfLutSavePath = "save/texture/brdf_lut.bin";

		constexpr auto format = VK_FORMAT_R16G16_SFLOAT;
		const uint32 blobDataSize = lutDim * lutDim * helper::getPixelSize(format);

		if (sEnableCacheBRDFLut && std::filesystem::exists(brdfLutSavePath))
		{
			std::vector<uint8> blobData;
			loadAsset(blobData, brdfLutSavePath);

			if (blobData.size() == blobDataSize)
			{
				auto imageCI = helper::buildBasicUploadImageCreateInfo(lutDim, lutDim);
				auto uploadVMACI = helper::buildVMAUploadImageAllocationCI();

				auto lut = getContext().getTexturePool().create("BRDF-LUT", lutDim, lutDim, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

				SizedBuffer buffer(blobData.size(), (void*)blobData.data());
				queue.uploadTexture(lut, buffer);
				return lut;
			}
		}

		VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		if (sEnableCacheBRDFLut)
		{
			usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}

		auto lut = getContext().getTexturePool().create("BRDF-LUT", lutDim, lutDim, format, usageFlags);

		BRDFLutPushConsts push { };
		push.texelSize = float2(1.0f / lutDim);
		push.UAV = asUAV(queue, lut);

		auto computeShader = getContext().getShaderLibrary().getShader<BRDFLutCS>();
		addComputePass2(queue,
			"BRDF-LUT",
			getContext().computePipe(computeShader, "BRDF-LUT"),
			push,
			{ lutDim, lutDim, 1 });

		if (sEnableCacheBRDFLut)
		{
			auto readBackBuffer = queue.copyImageToReadBackBuffer(lut);
			auto currentTimeline = queue.getCurrentTimeline();

			CallOnceInOneFrameEvent::add([readBackBuffer, currentTimeline, lutDim, blobDataSize, brdfLutSavePath](const ApplicationTickData& tickData, graphics::GraphicsQueue& graphics)
			{
				currentTimeline.waitFinish();

				std::vector<uint8> blobData(blobDataSize);

				readBackBuffer->get().map();

				readBackBuffer->get().invalidate();
				memcpy(blobData.data(), readBackBuffer->get().getMapped(), blobData.size());
				readBackBuffer->get().unmap();

				std::filesystem::create_directories(brdfLutSavePath.parent_path());
				saveAsset(blobData, ECompressionMode::Lz4, brdfLutSavePath, false);
			});
		}

		return lut;
	}

}