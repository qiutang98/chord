#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/tonemapping.hlsl>

using namespace chord;
using namespace chord::graphics;

PRIVATE_GLOBAL_SHADER(TonemappingPS, "resource/shader/tonemapping.hlsl", "mainPS", EShaderStage::Pixel);

void chord::tonemapping(
	uint32 cameraViewBufferId, 
	GraphicsQueue& queue, 
	PoolTextureRef srcImage, 
	PoolTextureRef outImage,
	PoolBufferGPUOnlyRef exposureBuffer)
{
	RenderTargets RTs { };
	RTs.RTs[0] = RenderTargetRT(outImage, ERenderTargetLoadStoreOp::Nope_Store);

	uint32 srcImageId = asSRV(queue, srcImage);

	TonemappingPushConsts pushConst { };
	pushConst.textureSize = { outImage->get().getExtent().width, outImage->get().getExtent().height };
	pushConst.cameraViewId = cameraViewBufferId;
	pushConst.textureId = srcImageId;
	pushConst.pointClampSamplerId = getSamplers().pointClampBorder0000().index.get();
	pushConst.SRV_exposure = asSRV(queue, exposureBuffer);
	addFullScreenPass2<TonemappingPS>(queue, "Tonemapping", RTs, pushConst);
}