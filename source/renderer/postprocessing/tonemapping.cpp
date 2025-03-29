#include <shader_compiler/shader.h>
#include <shader_compiler/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/tonemapping.hlsl>

namespace chord 
{ 
	namespace graphics
	{
		PRIVATE_GLOBAL_SHADER(TonemappingPS, "resource/shader/tonemapping.hlsl", "mainPS", EShaderStage::Pixel);
	}

	void chord::tonemapping(
		uint32 cameraViewBufferId,
		const PostprocessConfig& config,
		graphics::GraphicsQueue& queue,
		graphics::PoolTextureRef srcImage,
		graphics::PoolTextureRef outImage,
		graphics::PoolTextureRef bloomTex)
	{
		using namespace graphics;

		RenderTargets RTs { };
		RTs.RTs[0] = RenderTargetRT(outImage, ERenderTargetLoadStoreOp::Nope_Store);

		uint32 srcImageId = asSRV(queue, srcImage);

		TonemappingPushConsts pushConst { };
		pushConst.textureSize = { outImage->get().getExtent().width, outImage->get().getExtent().height };
		pushConst.cameraViewId = cameraViewBufferId;
		pushConst.textureId = srcImageId;
		pushConst.pointClampSamplerId = getSamplers().pointClampBorder0000().index.get();
		pushConst.bloomTexId = asSRV(queue, bloomTex);
		pushConst.bloomIntensity = config.bloomIntensity;

		addFullScreenPass2<TonemappingPS>(queue, "Tonemapping", RTs, pushConst);
	}

}