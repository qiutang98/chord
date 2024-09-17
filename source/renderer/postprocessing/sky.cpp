#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/sky.hlsl>

using namespace chord;
using namespace chord::graphics;

PRIVATE_GLOBAL_SHADER(RenderSkyCS, "resource/shader/sky.hlsl", "skyRenderCS", EShaderStage::Compute);

void chord::renderSky(
	GraphicsQueue& queue, 
	PoolTextureRef sceneColor, 
	PoolTextureRef depthImage,
	uint32 cameraViewId,
	const AtmosphereLut& luts)
{
	SkyPushConsts pushConst{};

	pushConst.cameraViewId  = cameraViewId;
	pushConst.depthId = asSRV(queue, depthImage, helper::buildDepthImageSubresource());
	pushConst.linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
	pushConst.sceneColorId  = asUAV(queue, sceneColor);

	pushConst.irradianceTextureId = asSRV(queue, luts.irradianceTexture);
	pushConst.transmittanceId = asSRV(queue, luts.transmittance);
	pushConst.scatteringId    = asSRV3DTexture(queue, luts.scatteringTexture);

	if (luts.optionalSingleMieScatteringTexture != nullptr)
	{
		pushConst.singleMieScatteringId = asSRV3DTexture(queue, luts.optionalSingleMieScatteringTexture);
	}

	auto computeShader = getContext().getShaderLibrary().getShader<RenderSkyCS>();
	addComputePass2(
		queue,
		"RenderSkyCS",
		getContext().computePipe(computeShader, "RenderSkyPipe"),
		pushConst,
		{ 
			divideRoundingUp(sceneColor->get().getExtent().width,  8U),
			divideRoundingUp(sceneColor->get().getExtent().height, 8U), 
			1 
		});
}