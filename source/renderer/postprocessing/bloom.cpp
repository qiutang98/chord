#include <shader_compiler/shader.h>
#include <shader_compiler/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/hzb.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <shader/bloom_downsample.hlsl>
#include <shader/bloom_upsample.hlsl>

using namespace chord;
using namespace chord::graphics;

constexpr uint32 kMaxDownsampleCount = 6;

class BloomDownSampleCS : public GlobalShader
{
public:
	DECLARE_SUPER_TYPE(GlobalShader);

	class SV_bFirstPass : SHADER_VARIANT_BOOL("DIM_PASS_0");
	using Permutation = TShaderVariantVector<SV_bFirstPass>;
};
IMPLEMENT_GLOBAL_SHADER(BloomDownSampleCS, "resource/shader/bloom_downsample.hlsl", "mainCS", EShaderStage::Compute);

class BloomUpSampleCS : public GlobalShader
{
public:
	DECLARE_SUPER_TYPE(GlobalShader);

	class SV_bCompositeUpsample : SHADER_VARIANT_BOOL("DIM_COMPOSITE_UPSAMPLE");
	using Permutation = TShaderVariantVector<SV_bCompositeUpsample>;
};
IMPLEMENT_GLOBAL_SHADER(BloomUpSampleCS, "resource/shader/bloom_upsample.hlsl", "mainCS", EShaderStage::Compute);

static inline math::vec4 getBloomPrefilter(float threshold, float thresholdSoft)
{
	// https://catlikecoding.com/unity/tutorials/advanced-rendering/bloom/
	float knee = threshold * thresholdSoft;
	math::vec4 prefilter{ };

	prefilter.x = threshold;
	prefilter.y = prefilter.x - knee;
	prefilter.z = 2.0f * knee;
	prefilter.w = 0.25f / (knee + 0.00001f);

	return prefilter;
}

PoolTextureRef chord::computeBloom(
	graphics::GraphicsQueue& queue,
	graphics::PoolTextureRef color, 
	const PostprocessConfig& config,
	uint cameraViewId)
{
	auto& pool = getContext().getTexturePool();


	uint2 dim = { color->get().getExtent().width, color->get().getExtent().height };

	const uint32 mipStartWidth  = dim.x >> 1;
	const uint32 mipStartHeight = dim.y >> 1;

	const uint32 downsampleMipCount = glm::min(kMaxDownsampleCount, std::bit_width(glm::min(mipStartWidth, mipStartHeight)) - 1U);

	std::vector<PoolTextureRef> downsampleTextures(downsampleMipCount);

	for (uint32 i = 0; i < downsampleMipCount; i++)
	{
		downsampleTextures[i] = pool.create(
			"SceneColorBlurChain",
			mipStartWidth  >> i,
			mipStartHeight >> i,
			VK_FORMAT_B10G11R11_UFLOAT_PACK32,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	}

	// Downsample. 
	{
		BloomDownSamplePushConsts pushConsts{};

		pushConsts.cameraViewId = cameraViewId;
		pushConsts.prefilterFactor = getBloomPrefilter(config.bloomThreshold, config.bloomThresholdSoft);

		//
		auto Input  = color;
		for (uint32 i = 0; i < downsampleMipCount; i++)
		{
			auto Output = downsampleTextures[i];

			pushConsts.workDim = { Output->get().getExtent().width, Output->get().getExtent().height };
			pushConsts.srcDim  = { Input->get().getExtent().width,  Input->get().getExtent().height  };
			pushConsts.UAV = asUAV(queue, Output);
			pushConsts.SRV = asSRV(queue, Input);

			const uint2 dispatchDim = divideRoundingUp(pushConsts.workDim, uint2(8));

			BloomDownSampleCS::Permutation permutation;
			permutation.set<BloomDownSampleCS::SV_bFirstPass>(i == 0);
			auto computeShader = getContext().getShaderLibrary().getShader<BloomDownSampleCS>(permutation);

			addComputePass2(queue,
				std::format("Downsample_{0}x{1}", pushConsts.workDim.x, pushConsts.workDim.y),
				getContext().computePipe(computeShader, "Downsample"),
				pushConsts,
				{ dispatchDim.x, dispatchDim.y, 1 });

			Input = Output;
		}
	}

	// Upsample.
	{
		BloomUpsamplePushConsts pushConst{};
		pushConst.cameraViewId = cameraViewId;
		pushConst.blurRadius = config.bloomRadius;
		pushConst.radius = config.bloomSampleCount;
		pushConst.sigma = config.bloomGaussianSigma;

		for (int i = downsampleMipCount - 2; i >= 0; i--)
		{
			auto Input = downsampleTextures[i + 1];
			auto Output = downsampleTextures[i];

			pushConst.workDim = { Output->get().getExtent().width, Output->get().getExtent().height };
			pushConst.srcDim  = { Input->get().getExtent().width, Input->get().getExtent().height };

			auto temp = pool.create(
				"SceneColorBlurChain-Temp",
				pushConst.workDim.x,
				pushConst.workDim.y,
				VK_FORMAT_B10G11R11_UFLOAT_PACK32,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);


			const uint2 dispatchDim = divideRoundingUp(pushConst.workDim, uint2(8));

			{
				pushConst.SRV = asSRV(queue, Input);
				pushConst.UAV = asUAV(queue, temp);

				pushConst.direction = { 1.0f, 0.0f };

				BloomUpSampleCS::Permutation permutation;
				permutation.set<BloomUpSampleCS::SV_bCompositeUpsample>(false);
				auto computeShader = getContext().getShaderLibrary().getShader<BloomUpSampleCS>(permutation);

				addComputePass2(queue,
					std::format("Upsample_x_{0}x{1}", pushConst.workDim.x, pushConst.workDim.y),
					getContext().computePipe(computeShader, "Upsample"),
					pushConst,
					{ dispatchDim.x, dispatchDim.y, 1 });
			}

			{
				pushConst.SRV = asSRV(queue, temp);
				pushConst.UAV = asUAV(queue, Output);

				pushConst.direction = { 0.0f, 1.0f };

				BloomUpSampleCS::Permutation permutation;
				permutation.set<BloomUpSampleCS::SV_bCompositeUpsample>(true);
				auto computeShader = getContext().getShaderLibrary().getShader<BloomUpSampleCS>(permutation);

				addComputePass2(queue,
					std::format("Upsample_y_{0}x{1}", pushConst.workDim.x, pushConst.workDim.y),
					getContext().computePipe(computeShader, "Upsample"),
					pushConst,
					{ dispatchDim.x, dispatchDim.y, 1 });
			}
		}
	}

	return downsampleTextures[0];
}