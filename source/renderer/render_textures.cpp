#include <renderer/render_textures.h>

namespace chord
{
    static constexpr auto kGBufferVkImageUsage 
        = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 
        | VK_IMAGE_USAGE_SAMPLED_BIT 
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT 
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    static constexpr auto kDepthVkImageUsage 
        = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 
        | VK_IMAGE_USAGE_SAMPLED_BIT 
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    static constexpr auto kHalfGbufferImageUsage
        = VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_STORAGE_BIT;

    GBufferTextures chord::allocateGBufferTextures(uint32 width, uint32 height)
    {
        ZoneScopedN("allocateGBufferTextures");
        using namespace graphics;

        GBufferTextures result { };
        auto& pool = getContext().getTexturePool();

        result.dimension = { width, height };

        // Visibility.
        result.visibility = pool.create("Gbuffer.Visibility", width, height, GBufferTextures::visibilityFormat(), kGBufferVkImageUsage);

        // Color and depth stencil.
        result.color = pool.create("Gbuffer.Color", width, height, GBufferTextures::colorFormat(), kGBufferVkImageUsage | VK_IMAGE_USAGE_STORAGE_BIT);
        result.depthStencil = pool.create("Gbuffer.DepthStencil", width, height, GBufferTextures::depthStencilFormat(), kDepthVkImageUsage);

        // Extract gbuffer.
        result.vertexRSNormal = pool.create("Gbuffer.vertexRSNormal", width, height, GBufferTextures::vertexRSNormalFormat(), kGBufferVkImageUsage | VK_IMAGE_USAGE_STORAGE_BIT);
        result.pixelRSNormal  = pool.create("Gbuffer.pixelRSNormal", width, height, GBufferTextures::pixelRSNormalFormat(), kGBufferVkImageUsage | VK_IMAGE_USAGE_STORAGE_BIT);
        result.motionVector   = pool.create("Gbuffer.motionVector", width, height, GBufferTextures::motionVectorFormat(), kGBufferVkImageUsage | VK_IMAGE_USAGE_STORAGE_BIT);
        result.aoRoughnessMetallic = pool.create("Gbuffer.aoRoughnessMetallic", width, height, GBufferTextures::aoRoughnessMetallicFormat(), kGBufferVkImageUsage | VK_IMAGE_USAGE_STORAGE_BIT);
        result.baseColor      = pool.create("Gbuffer.baseColor", width, height, GBufferTextures::baseColorFormat(), kGBufferVkImageUsage | VK_IMAGE_USAGE_STORAGE_BIT);
        // 
        return result;
    }

    void chord::GBufferTextures::generateHalfGbuffer()
    {
        using namespace graphics;
        check(dimension.x != 0 && dimension.y != 0);

        uint halfWidth  = dimension.x / 2;
        uint halfHeight = dimension.y / 2;

        auto& pool = getContext().getTexturePool();

        // Half resolution gbuffers.
        check(depth_Half == nullptr);
        depth_Half = pool.create("Gbuffer.depth_Half", halfWidth, halfHeight, GBufferTextures::depthHalfFormat(), kHalfGbufferImageUsage);

        check(pixelRSNormal_Half == nullptr);
        pixelRSNormal_Half = pool.create("Gbuffer.pixelRSNormal_Half", halfWidth, halfHeight, GBufferTextures::pixelRSNormalFormat(), kHalfGbufferImageUsage);

        check(vertexRSNormal_Half == nullptr);
        vertexRSNormal_Half = pool.create("Gbuffer.vertexRSNormal_Half", halfWidth, halfHeight, GBufferTextures::vertexRSNormalFormat(), kHalfGbufferImageUsage);

        check(motionVector_Half == nullptr);
        motionVector_Half = pool.create("Gbuffer.motionVector_Half", halfWidth, halfHeight, GBufferTextures::motionVectorFormat(), kHalfGbufferImageUsage);

        check(roughness_Half == nullptr);
        roughness_Half = pool.create("Gbuffer.roughness_Half", halfWidth, halfHeight, GBufferTextures::roughnessHalfFormat(), kHalfGbufferImageUsage);
    }

    void chord::addClearGbufferPass(graphics::GraphicsQueue& queue, GBufferTextures& textures)
    {
        using namespace graphics;
        ScopePerframeMarker marker(queue, "Clear Gbuffers");

        queue.checkRecording();
        {
            static const auto clearValue = VkClearColorValue{ .uint32 = { 0, 0, 0, 0} };
            static const auto range = helper::buildBasicImageSubresource();

            // Visibility.
            queue.clearImage(textures.visibility, &clearValue, 1, &range);

            // Color.
            queue.clearImage(textures.color, &clearValue, 1, &range);

            // Extract gbuffer.
            queue.clearImage(textures.vertexRSNormal,      &clearValue, 1, &range);
            queue.clearImage(textures.pixelRSNormal,       &clearValue, 1, &range);
            queue.clearImage(textures.motionVector,        &clearValue, 1, &range);
            queue.clearImage(textures.aoRoughnessMetallic, &clearValue, 1, &range);
            queue.clearImage(textures.baseColor,           &clearValue, 1, &range);
        }

        {
            static const auto depthStencilClear = VkClearDepthStencilValue{ 0.0f, 0 };
            queue.clearDepthStencil(textures.depthStencil, &depthStencilClear);
        }
    }

}

