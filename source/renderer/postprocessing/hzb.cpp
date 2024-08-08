#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/hzb.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>

namespace chord
{
	using namespace graphics;

    class HZBCS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);

        class SV_MipCount : SHADER_VARIANT_RANGE_INT("MIP_COUNT", 1, 12);
        using Permutation = TShaderVariantVector<SV_MipCount>;
    };
    IMPLEMENT_GLOBAL_SHADER(HZBCS, "resource/shader/hzb.hlsl", "mainCS", EShaderStage::Compute);
    
    HZBContext chord::buildHZB(GraphicsQueue& queue, PoolTextureRef depthImage)
    {
        uint32 depthWdith = depthImage->get().getExtent().width;
        uint32 depthHeight = depthImage->get().getExtent().height;

        uint32 targetWidth = getNextPOT(depthWdith) / 2;
        uint32 targetHeight = getNextPOT(depthHeight) / 2;

        PoolTextureCreateInfo ci{};
        ci.format    = VK_FORMAT_R16_SFLOAT;
        ci.extent    = { targetWidth, targetHeight, 1 };
        ci.usage     = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.mipLevels = getMipLevelsCount(targetWidth, targetHeight);

        PoolTextureRef hzbMinTexture = getContext().getTexturePool().create("HZB-Min-Texture", ci);
        PoolTextureRef hzbMaxTexture = getContext().getTexturePool().create("HZB-Max-Texture", ci);

        auto countBuffer = getContext().getBufferPool().createGPUOnly(
            "hzb downsample count buffer",
            sizeof(uint),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        queue.clearUAV(countBuffer);

        HZBPushConst pushConst{ };
        for (uint i = 0; i < ci.mipLevels; i++)
        {
            VkImageSubresourceRange range = helper::buildBasicImageSubresource();
            range.baseMipLevel = i;
            range.levelCount   = 1;

            pushConst.hzbMinView[i] = asUAV(queue, hzbMinTexture, range);
            pushConst.hzbMaxView[i] = asUAV(queue, hzbMaxTexture, range);
        }
        pushConst.sceneDepth = asSRV(queue, depthImage, helper::buildDepthImageSubresource());

        uint3 dispatchParam = { (targetWidth  + 31) / 32, (targetHeight + 31) / 32, 1 };

        pushConst.numWorkGroups = dispatchParam.x * dispatchParam.y;
        pushConst.sceneDepthWidth = depthWdith;
        pushConst.sceneDepthHeight = depthHeight;

        HZBCS::Permutation permutation;
        permutation.set<HZBCS::SV_MipCount>(ci.mipLevels);
        auto computeShader = getContext().getShaderLibrary().getShader<HZBCS>(permutation);

        check(ci.mipLevels >= 6);
        addComputePass(queue,
            "HZB: MinMaxBuild",
            getContext().computePipe(computeShader, "HZB: MinMaxBuild", { 
                getContext().descriptorFactoryBegin()
                .textureUAV(0) // mip5DestMin
                .textureUAV(1) // mip5DestMax
                .buffer(2)     // counterBuffer
                .buildNoInfoPush() }),
            dispatchParam, 
            [&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
            {
                pipe->pushConst(cmd, pushConst);

                VkImageSubresourceRange range = helper::buildBasicImageSubresource();
                range.baseMipLevel = 5;
                range.levelCount = 1;

                PushSetBuilder(queue, cmd)
                    .addUAV(hzbMinTexture, range) // mip5DestMin
                    .addUAV(hzbMaxTexture, range) // mip5DestMax
                    .addUAV(countBuffer)          // counterBuffer
                    .push(pipe, 1); // Push set 1.
            });

        const uvec2 mip0Dimension = { targetWidth, targetHeight };
        const uvec2 mip0ValidArea = { (depthWdith + 1) / 2, (depthHeight + 1) / 2 };
        const vec2 validUv = math::clamp(vec2(mip0ValidArea) / vec2(mip0Dimension), 0.0f, 1.0f);

        HZBContext result(
            hzbMinTexture, 
            hzbMaxTexture,
            mip0Dimension,
            ci.mipLevels,
            mip0ValidArea,
            validUv);

        return result;
    }
}