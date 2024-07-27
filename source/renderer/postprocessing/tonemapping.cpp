#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/tonemapping.hlsl>

namespace chord
{
	using namespace graphics;
	PRIVATE_GLOBAL_SHADER(TonemappingPS, "resource/shader/tonemapping.hlsl", "mainPS", EShaderStage::Pixel);


	void chord::tonemapping(GraphicsQueue& queue, PoolTextureRef srcImage, PoolTextureRef outImage)
	{
		RenderTargets RTs { };
		RTs.RTs[0] = RenderTargetRT(outImage, ERenderTargetLoadStoreOp::Nope_Store);

		uint32 srcImageId = asSRV(queue, srcImage);

		TonemappingPushConsts pushConst { };
		pushConst.textureId = srcImageId;
		pushConst.pointClampSamplerId = getSamplers().pointClampBorder0000().index.get();

		addFullScreenPass2<TonemappingPS>(queue, "Tonemapping", RTs, pushConst);
	}

}