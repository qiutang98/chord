#include <renderer/render_textures.h>

namespace chord
{
    using namespace graphics;

    static constexpr auto kGBufferVkImageUsage 
        = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 
        | VK_IMAGE_USAGE_SAMPLED_BIT 
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT 
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    static constexpr auto kDepthVkImageUsage 
        = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 
        | VK_IMAGE_USAGE_SAMPLED_BIT 
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    GBufferTextures chord::allocateGBufferTextures(uint32 width, uint32 height)
    {
        GBufferTextures result { };
        auto& pool = getContext().getTexturePool();

        // Color and depth stencil.
        result.color = pool.create("Gbuffer.Color", width, height, GBufferTextures::colorFormat(), kGBufferVkImageUsage);
        result.depthStencil = pool.create("Gbuffer.DepthStencil", width, height, GBufferTextures::depthStencilFormat(), kDepthVkImageUsage);

        // Extract gbuffer.
        result.gbufferA = pool.create("Gbuffer.A", width, height, GBufferTextures::gbufferAFormat(), kGBufferVkImageUsage);
        result.gbufferB = pool.create("Gbuffer.B", width, height, GBufferTextures::gbufferBFormat(), kGBufferVkImageUsage);
        result.gbufferC = pool.create("Gbuffer.C", width, height, GBufferTextures::gbufferCFormat(), kGBufferVkImageUsage);

        return result;
    }

    void chord::addClearGbufferPass(GraphicsQueue& queue, GBufferTextures& textures)
    {
        queue.checkRecording();
        {
            static const auto clearValue = VkClearColorValue{ .uint32 = { 0, 0, 0, 0} };
            static const auto range = helper::buildBasicImageSubresource();

            // Color.
            queue.clearImage(textures.color, &clearValue, 1, &range);

            // Extract gbuffer.
            queue.clearImage(textures.gbufferA, &clearValue, 1, &range);
            queue.clearImage(textures.gbufferB, &clearValue, 1, &range);
            queue.clearImage(textures.gbufferC, &clearValue, 1, &range);
        }

        {
            static const auto depthStencilClear = VkClearDepthStencilValue{ 0.0f, 0 };
            queue.clearDepthStencil(textures.depthStencil, &depthStencilClear);
        }
    }

}

