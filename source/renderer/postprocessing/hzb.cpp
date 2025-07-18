#include <shader_compiler/shader.h>
#include <shader_compiler/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/hzb.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <shader/hzb_one.hlsl>

namespace chord::graphics
{
    class SV_MipCount : SHADER_VARIANT_RANGE_INT("MIP_COUNT", 1, 12);

    class HZBCS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);

        class SV_bComputeValidRange : SHADER_VARIANT_BOOL("DIM_COMPUTE_VALID_RANGE");
        using Permutation = TShaderVariantVector<SV_bComputeValidRange, SV_MipCount>;
    };
    IMPLEMENT_GLOBAL_SHADER(HZBCS, "resource/shader/hzb.hlsl", "mainCS", EShaderStage::Compute);

    class HZBOneCS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);

        class SV_ReductionType : SHADER_VARIANT_SPARSE_INT("REDUCE_TYPE", REDUCE_TYPE_MIN, REDUCE_TYPE_MAX);
        using Permutation = TShaderVariantVector<SV_MipCount, SV_ReductionType>;
    };
    IMPLEMENT_GLOBAL_SHADER(HZBOneCS, "resource/shader/hzb_one.hlsl", "mainCS", EShaderStage::Compute);

}

namespace chord
{
    HZBContext chord::buildHZB(graphics::GraphicsQueue& queue, graphics::PoolTextureRef depthImage, bool bBuildMin, bool bBuildMax, bool bBuildValidRange)
    {
        ZoneScoped;
        using namespace graphics;

        // At least need to build one type.
        check(bBuildMin || bBuildMax);

        // When need to compute valid range, always need build two state.
        if (bBuildValidRange) { check(bBuildMin && bBuildMax); }

        uint32 depthWdith  = depthImage->get().getExtent().width;
        uint32 depthHeight = depthImage->get().getExtent().height;

        uint32 targetWidth  = getNextPOT(depthWdith)  / 2;
        uint32 targetHeight = getNextPOT(depthHeight) / 2;

        if (targetWidth == depthWdith) { targetWidth /= 2; }
        if (targetHeight == depthHeight) { targetHeight /= 2; }

        PoolTextureCreateInfo ci{};
        ci.format    = VK_FORMAT_R16_SFLOAT;
        ci.extent    = { targetWidth, targetHeight, 1 };
        ci.usage     = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.mipLevels = getMipLevelsCount(targetWidth, targetHeight);

        PoolTextureRef hzbMinTexture = nullptr; if (bBuildMin) { hzbMinTexture = getContext().getTexturePool().create("HZB-Min-Texture", ci); }
        PoolTextureRef hzbMaxTexture = nullptr; if (bBuildMax) { hzbMaxTexture = getContext().getTexturePool().create("HZB-Max-Texture", ci); }

        auto countBuffer = getContext().getBufferPool().createGPUOnly(
            "hzb downsample count buffer",
            sizeof(uint),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        queue.clearUAV(countBuffer, 0);

        uint3 dispatchParam = { (targetWidth + 31) / 32, (targetHeight + 31) / 32, 1 };

        if (bBuildMin && bBuildMax)
        {
            HZBPushConst pushConst{ };
            for (uint i = 0; i < ci.mipLevels; i++)
            {
                VkImageSubresourceRange range = helper::buildBasicImageSubresource();
                range.baseMipLevel = i;
                range.levelCount = 1;

                pushConst.hzbMinView[i] = asUAV(queue, hzbMinTexture, range);
                pushConst.hzbMaxView[i] = asUAV(queue, hzbMaxTexture, range);
            }

            if (isDepthFormat(depthImage))
            {
                pushConst.sceneDepth = asSRV(queue, depthImage, helper::buildDepthImageSubresource());
            }
            else
            {
                pushConst.sceneDepth = asSRV(queue, depthImage);
            }

            pushConst.numWorkGroups = dispatchParam.x * dispatchParam.y;
            pushConst.sceneDepthWidth = depthWdith;
            pushConst.sceneDepthHeight = depthHeight;

            PoolBufferRef validDepthMinMaxDepthBuffer = nullptr;
            if (bBuildValidRange)
            {
                validDepthMinMaxDepthBuffer = getContext().getBufferPool().createGPUOnly("MaxMinDepthBuffer", sizeof(uint2), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
                
                uint clearRangeValue[2] = { ~0u, 0u }; // Min & Max.
                queue.updateUAV(validDepthMinMaxDepthBuffer, 0, sizeof(uint2), clearRangeValue);

                pushConst.validDepthMinMaxBufferId = asUAV(queue, validDepthMinMaxDepthBuffer);
            }

            HZBCS::Permutation permutation;
            permutation.set<SV_MipCount>(ci.mipLevels);
            permutation.set<HZBCS::SV_bComputeValidRange>(bBuildValidRange);

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

            const math::uvec2 mip0Dimension = { targetWidth, targetHeight };
            const math::uvec2 mip0ValidArea = { depthWdith / 2, depthHeight / 2 };
            const math::vec2 validUv = math::clamp(math::vec2(mip0ValidArea) / math::vec2(mip0Dimension), 0.0f, 1.0f);

            HZBContext result(
                hzbMinTexture,
                hzbMaxTexture,
                validDepthMinMaxDepthBuffer,
                mip0Dimension,
                ci.mipLevels,
                mip0ValidArea,
                validUv);
            return result;
        }
        else
        {
            check(!bBuildValidRange);

            HZBOnePushConst pushConst{ };
            for (uint i = 0; i < ci.mipLevels; i++)
            {
                VkImageSubresourceRange range = helper::buildBasicImageSubresource();
                range.baseMipLevel = i;
                range.levelCount = 1;
                pushConst.hzbView[i] = asUAV(queue, bBuildMin ? hzbMinTexture : hzbMaxTexture, range);
            }

            if (isDepthFormat(depthImage))
            {
                pushConst.sceneDepth = asSRV(queue, depthImage, helper::buildDepthImageSubresource());
            }
            else
            {
                pushConst.sceneDepth = asSRV(queue, depthImage);
            }

            pushConst.numWorkGroups = dispatchParam.x * dispatchParam.y;
            pushConst.sceneDepthWidth = depthWdith;
            pushConst.sceneDepthHeight = depthHeight;

            HZBOneCS::Permutation permutation;
            permutation.set<SV_MipCount>(ci.mipLevels);
            permutation.set<HZBOneCS::SV_ReductionType>(bBuildMin ? REDUCE_TYPE_MIN : REDUCE_TYPE_MAX);

            auto computeShader = getContext().getShaderLibrary().getShader<HZBOneCS>(permutation);
            check(ci.mipLevels >= 6);
            addComputePass(queue,
                bBuildMin ? "HZB: MinBuild" : "HZB: MaxBuild",
                getContext().computePipe(computeShader, bBuildMin ? "HZB: MinBuild" : "HZB: MaxBuild", {
                    getContext().descriptorFactoryBegin()
                    .textureUAV(0) // mip5Dest
                    .buffer(1)     // counterBuffer
                    .buildNoInfoPush() }),
                dispatchParam,
                [&](GraphicsOrComputeQueue& queue, ComputePipelineRef pipe, VkCommandBuffer cmd)
                {
                    pipe->pushConst(cmd, pushConst);

                    VkImageSubresourceRange range = helper::buildBasicImageSubresource();
                    range.baseMipLevel = 5;
                    range.levelCount = 1;

                    PushSetBuilder(queue, cmd)
                        .addUAV(bBuildMin ? hzbMinTexture : hzbMaxTexture, range) // mip5Dest
                        .addUAV(countBuffer)  // counterBuffer
                        .push(pipe, 1); // Push set 1.
                });

            const math::uvec2 mip0Dimension = { targetWidth, targetHeight };
            const math::uvec2 mip0ValidArea = { depthWdith / 2, depthHeight / 2 };
            const math::vec2 validUv = math::clamp(math::vec2(mip0ValidArea) / math::vec2(mip0Dimension), 0.0f, 1.0f);

            HZBContext result(
                hzbMinTexture,
                hzbMaxTexture,
                nullptr,
                mip0Dimension,
                ci.mipLevels,
                mip0ValidArea,
                validUv);
            return result;
        }

    }
}