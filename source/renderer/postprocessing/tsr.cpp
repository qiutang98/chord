#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/hzb.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <shader/tsr_prepare.hlsl>
#include <shader/tsr_rectify.hlsl>
#include <shader/tsr_reprojection.hlsl>
#include <shader/tsr_sharpen.hlsl>

using namespace chord;
using namespace chord::graphics;

PRIVATE_GLOBAL_SHADER(TSRPrepareCS, "resource/shader/tsr_prepare.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(TSRRectifyCS, "resource/shader/tsr_rectify.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(TSRReprojectionCS, "resource/shader/tsr_reprojection.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(TSRSharpeness, "resource/shader/tsr_sharpen.hlsl", "mainCS", EShaderStage::Compute);

static float sTSRSharpeness = 0.5f;
static AutoCVarRef cVarTSRSharpeness(
	"r.tsr.sharpness",
	sTSRSharpeness,
	"Sharpeness for TSR."
);

TSRResult chord::computeTSR(
	GraphicsQueue& queue, 
	PoolTextureRef color, 
	PoolTextureRef depth, 
	PoolTextureRef motionVector,
	PoolTextureRef historyLowResColor,
	uint32 cameraViewId,
	const PerframeCameraView& perframe)
{
	uint2 renderDim = { color->get().getExtent().width, color->get().getExtent().height };

	PoolTextureRef dilateVelocity = nullptr;
	PoolTextureRef reprojectedHistory = nullptr;
	if (historyLowResColor != nullptr)
	{
		dilateVelocity = getContext().getTexturePool().create("DilateVelocity", renderDim.x, renderDim.y, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
		{
			TSRPreparePushConsts pushConsts{};
			pushConsts.gbufferDim = renderDim;
			pushConsts.cameraViewId = cameraViewId;
			pushConsts.depthSRV = asSRV(queue, depth, helper::buildDepthImageSubresource());
			pushConsts.motionVectorSRV = asSRV(queue, motionVector);
			pushConsts.UAV = asUAV(queue, dilateVelocity);

			const uint2 dispatchDim = divideRoundingUp(renderDim, uint2(8));

			auto computeShader = getContext().getShaderLibrary().getShader<TSRPrepareCS>();
			addComputePass2(queue,
				"TSRPrepareCS",
				getContext().computePipe(computeShader, "TSRPrepareCS"),
				pushConsts,
				{ dispatchDim.x, dispatchDim.y, 1 });
		}

		reprojectedHistory = getContext().getTexturePool().create("ReprojectedHistory", renderDim.x, renderDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
		{
			TSRReprojectionPushConsts pushConsts{};

			pushConsts.gbufferDim = renderDim;
			pushConsts.cameraViewId = cameraViewId;
			pushConsts.closestMotionVectorId = asSRV(queue, dilateVelocity);
			pushConsts.historyColorId = asSRV(queue, historyLowResColor);
			pushConsts.UAV = asUAV(queue, reprojectedHistory);

			const uint2 dispatchDim = divideRoundingUp(renderDim, uint2(16));

			auto computeShader = getContext().getShaderLibrary().getShader<TSRReprojectionCS>();
			addComputePass2(queue,
				"TSRReprojectionCS",
				getContext().computePipe(computeShader, "TSRReprojectionCS"),
				pushConsts,
				{ dispatchDim.x, dispatchDim.y, 1 });
		}
	}

	auto lowResolveColor = getContext().getTexturePool().create("ResolveTAA", renderDim.x, renderDim.y, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	{
		TSRRecitfyPushConsts pushConsts{};

		pushConsts.gbufferDim = renderDim;
		pushConsts.cameraViewId = cameraViewId;
		pushConsts.hdrSceneColorId = asSRV(queue, color);


		pushConsts.closestMotionVectorId = dilateVelocity == nullptr ? kUnvalidIdUint32 : asSRV(queue, dilateVelocity);
		pushConsts.reprojectedId = reprojectedHistory == nullptr ? kUnvalidIdUint32 : asSRV(queue, reprojectedHistory);

		pushConsts.UAV = asUAV(queue, lowResolveColor);

		// Precompute weights used for the Blackman-Harris filter.
		{
			float totalWeight = 0.0f;
			for (int i = 0; i < 9; ++i)
			{
				float x = kTSRRectifyNeighbourOffsets[i].x + perframe.jitterData.x;
				float y = kTSRRectifyNeighbourOffsets[i].y + perframe.jitterData.y;
				float d = (x * x + y * y);

				pushConsts.blackmanHarrisWeights[i] = math::exp((-0.5f / (0.22f)) * d);
				totalWeight += pushConsts.blackmanHarrisWeights[i];
			}

			for (int i = 0; i < 9; ++i)
			{
				pushConsts.blackmanHarrisWeights[i] /= totalWeight;
			}
		}

		const uint2 dispatchDim = divideRoundingUp(renderDim, uint2(8));

		auto computeShader = getContext().getShaderLibrary().getShader<TSRRectifyCS>();
		addComputePass2(queue,
			"TSRRectifyCS",
			getContext().computePipe(computeShader, "TSRRectifyCS"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	auto sharpenColor = getContext().getTexturePool().create("sharpenColor", renderDim.x, renderDim.y, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	{
		TSRSharpenPushConsts pushConsts{};
		pushConsts.gbufferDim = renderDim;
		pushConsts.cameraViewId = cameraViewId;
		pushConsts.sharpeness = sTSRSharpeness;
		pushConsts.SRV = asSRV(queue, lowResolveColor);
		pushConsts.UAV = asUAV(queue, sharpenColor);

		const uint2 dispatchDim = divideRoundingUp(renderDim, uint2(16));

		auto computeShader = getContext().getShaderLibrary().getShader<TSRSharpeness>();
		addComputePass2(queue,
			"TSRSharpeness",
			getContext().computePipe(computeShader, "TSRSharpeness"),
			pushConsts,
			{ dispatchDim.x, dispatchDim.y, 1 });
	}

	TSRResult result{};
	result.history.lowResResolveTAAColor = lowResolveColor;
	result.presentColor = sharpenColor;

	return result;
}