#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>

#include <shader/brdf_lut.hlsl>
#include <renderer/lighting.h>

using namespace chord;
using namespace chord::graphics;


PRIVATE_GLOBAL_SHADER(BRDFLutCS, "resource/shader/brdf_lut.hlsl", "mainCS", EShaderStage::Compute);

graphics::PoolTextureRef chord::computeBRDFLut(graphics::GraphicsOrComputeQueue& queue, uint lutDim)
{
	constexpr const char* brdfLutSavePath = "save/texture/brdf_lut.bin";
	if (std::filesystem::exists(brdfLutSavePath))
	{
		// return getContext().getTexturePool().load("BRDF-LUT", brdfLutSavePath);

		auto stageBuffer = createStageUploadBuffer(texture.getName() + "-syncStageBuffer", data);

		executeImmediatelyMajorGraphics([&](VkCommandBuffer cmd, uint32 family, VkQueue queue)
			{
				const auto range = helper::buildBasicImageSubresource();

				GPUTextureSyncBarrierMasks copyState{};
				copyState.barrierMasks.queueFamilyIndex = family;
				copyState.barrierMasks.accesMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				copyState.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				texture.transition(cmd, copyState, range);

				VkBufferImageCopy region{};
				region.bufferOffset = 0;
				region.bufferRowLength = 0;
				region.bufferImageHeight = 0;
				region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				region.imageSubresource.mipLevel = 0;
				region.imageSubresource.baseArrayLayer = 0;
				region.imageSubresource.layerCount = 1;
				region.imageOffset = { 0, 0, 0 };
				region.imageExtent = texture.getExtent();
				vkCmdCopyBufferToImage(cmd, *stageBuffer, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

				GPUTextureSyncBarrierMasks finalState{};
				finalState.barrierMasks.queueFamilyIndex = family;
				finalState.barrierMasks.accesMask = VK_ACCESS_SHADER_READ_BIT;
				finalState.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				texture.transition(cmd, finalState, range);
			});
	}

	constexpr auto format = VK_FORMAT_R16G16_SFLOAT;
	auto lut = getContext().getTexturePool().create(
		"BRDF-LUT",
		lutDim,
		lutDim, format,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

	BRDFLutPushConsts push { };
	push.texelSize = float2(1.0f / lutDim);
	push.UAV = asUAV(queue, lut);

	auto computeShader = getContext().getShaderLibrary().getShader<BRDFLutCS>();
	addComputePass2(queue,
		"BRDF-LUT",
		getContext().computePipe(computeShader, "BRDF-LUT"),
		push,
		{ lutDim, lutDim, 1 });
	asSRV(queue, lut);

	auto readBackBuffer = queue.copyImageToReadBackBuffer(lut);
	auto currentTimeline = queue.getCurrentTimeline();

	CallOnceInOneFrameEvent::add([readBackBuffer, currentTimeline, lutDim](const ApplicationTickData& tickData, graphics::GraphicsQueue& graphics)
	{
		currentTimeline.waitFinish();

		std::vector<uint8> blobData(lutDim * lutDim * helper::getPixelSize(format));

		readBackBuffer->get().map();

		readBackBuffer->get().invalidate();
		memcpy(blobData.data(), readBackBuffer->get().getMapped(), blobData.size());
		readBackBuffer->get().unmap();

		// saveEXR<helper::R16G16, TINYEXR_PIXELTYPE_HALF>((const helper::R16G16*)blobData.data(), lutDim, lutDim, brdfLutSavePath);
	});

	return lut;
}