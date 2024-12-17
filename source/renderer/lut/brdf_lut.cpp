#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>

#include <shader/brdf_lut.hlsl>
#include <renderer/lighting.h>

using namespace chord;
using namespace chord::graphics;


PRIVATE_GLOBAL_SHADER(BRDFLutCS, "resource/shader/brdf_lut.hlsl", "mainCS", EShaderStage::Compute);

graphics::PoolTextureRef chord::computeBRDFLut(graphics::GraphicsOrComputeQueue& queue, uint lutDim)
{
	auto lut = getContext().getTexturePool().create(
		"BRDF-LUT",
		lutDim,
		lutDim, VK_FORMAT_R32G32_SFLOAT,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

	BRDFLutPushConsts push { };
	push.texelSize = float2(1.0f / lutDim);
	push.UAV = asUAV(queue, lut);

	auto computeShader = getContext().getShaderLibrary().getShader<BRDFLutCS>();
	addComputePass2(queue,
		"BRDF-LUT",
		getContext().computePipe(computeShader, "BRDF-LUT"),
		push,
		{ lutDim / 8, lutDim / 8, 1 });
	asSRV(queue, lut);

	return lut;
}