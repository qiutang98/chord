#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/gltf_basepass.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>

namespace chord
{
    using namespace graphics;

    static uint32 sGLTFRenderingShaderDebugMode = 0;
    static AutoCVarRef cVarGLTFRenderingShaderDebugMode(
        "r.gltf.rendering.shaderDebugMode",
        sGLTFRenderingShaderDebugMode,
        "**** GLTF rendering shader debug mode ****"
        "  0. default state, do nothing."
        "  1. siwtch triangle draw mode (persistent state)."
    );

    uint32 handleGLTFRenderShaderDebugMode()
    {
        auto oldValue = sGLTFRenderingShaderDebugMode;
        sGLTFRenderingShaderDebugMode = 0;
        return oldValue;
    }

    class GLTFBasePassObjectCullCS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);
    };

    class GLTFBasePassMeshletCullCS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);
    };

    class GLTFBasePassRenderingVS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);
    };

    class GLTFBasePassRenderingPS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);
    };

    IMPLEMENT_GLOBAL_SHADER(GLTFBasePassRenderingPS, "resource/shader/gltf_basepass.hlsl", "mainPS", EShaderStage::Pixel);
    IMPLEMENT_GLOBAL_SHADER(GLTFBasePassRenderingVS, "resource/shader/gltf_basepass.hlsl", "mainVS", EShaderStage::Vertex);
    IMPLEMENT_GLOBAL_SHADER(GLTFBasePassMeshletCullCS, "resource/shader/gltf_basepass.hlsl", "meshletCullingCS", EShaderStage::Compute);
    IMPLEMENT_GLOBAL_SHADER(GLTFBasePassObjectCullCS, "resource/shader/gltf_basepass.hlsl", "perobjectCullingCS", EShaderStage::Compute);
    PRIVATE_GLOBAL_SHADER(GLTFBasePassFillMeshletCullCS, "resource/shader/gltf_basepass.hlsl", "fillMeshletCullCmdCS", EShaderStage::Compute);

    void chord::gltfBasePassRendering(
        graphics::GraphicsQueue& queue, 
        GBufferTextures& gbuffers, 
        uint32 cameraView,
        const GLTFRenderDescriptor& gltfRenderDescriptor)
    {
        if (gltfRenderDescriptor.gltfObjectCount == 0)
        {
            // When no gltf object place in scene, we just skip.
            return;
        }

        const uint kObjectCount = gltfRenderDescriptor.gltfObjectCount;
        const uint kMaxMeshletCount = gltfRenderDescriptor.perframeCollect->gltfLod0MeshletCount;

        check(kObjectCount == gltfRenderDescriptor.perframeCollect->gltfPrimitives.size());
        check(kMaxMeshletCount > 0);
        queue.checkRecording();

        GLTFDrawPushConsts pushTemplate{};
        pushTemplate.cameraViewId = cameraView;
        pushTemplate.debugFlags = handleGLTFRenderShaderDebugMode();

        // #0. Object level culling.
        auto meshletCullGroupCountBuffer = getContext().getBufferPool().createGPUOnly(
            "meshletCullGroupCountBuffer",
            sizeof(uint),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto meshletCullGroupDetailBuffer = getContext().getBufferPool().createGPUOnly(
            "meshletCullGroupDetailBuffer",
            sizeof(uvec2) * kMaxMeshletCount,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        queue.clearUAV(meshletCullGroupCountBuffer);
        {
            GLTFDrawPushConsts push = pushTemplate;
            push.meshletCullGroupCountId = asUAV(queue, meshletCullGroupCountBuffer);
            push.meshletCullGroupDetailId = asUAV(queue, meshletCullGroupDetailBuffer);

            auto computeShader = getContext().getShaderLibrary().getShader<GLTFBasePassObjectCullCS>();

            addComputePass2(queue,
                "GLTFBasePass: ObjectCull",
                getContext().computePipe(computeShader, "GLTFBasePassPipe: ObjectCull"),
                push,
                math::uvec3(divideRoundingUp(kObjectCount, 64U), 1, 1));
        }

        // #1. Fill meshlet culling param.
        auto meshletCullCmdBuffer = getContext().getBufferPool().createGPUOnly(
            "meshletCullCmdBuffer",
            sizeof(uvec4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        {
            GLTFDrawPushConsts push = pushTemplate;
            push.meshletCullGroupCountId = asSRV(queue, meshletCullGroupCountBuffer);
            push.meshletCullGroupDetailId = asSRV(queue, meshletCullGroupDetailBuffer);
            push.meshletCullCmdId = asUAV(queue, meshletCullCmdBuffer);
            auto computeShader = getContext().getShaderLibrary().getShader<GLTFBasePassFillMeshletCullCS>();

            addComputePass2(queue,
                "GLTFBasePass: FillMeshletCullParam",
                getContext().computePipe(computeShader, "GLTFBasePassPipe: FillMeshletCullParam"),
                push,
                math::uvec3(1, 1, 1));
        }

        // #2. Meshlet level culling.
        auto drawMeshletCountBuffer = getContext().getBufferPool().createGPUOnly(
            "drawMeshletCountBuffer",
            sizeof(uint),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto drawMeshletCmdBuffer = getContext().getBufferPool().createGPUOnly(
            "drawMeshletCmdBuffer",
            sizeof(GLTFMeshDrawCmd) * kMaxMeshletCount,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        queue.clearUAV(drawMeshletCountBuffer);
        {
            GLTFDrawPushConsts push = pushTemplate;

            push.meshletCullGroupCountId = asSRV(queue, meshletCullGroupCountBuffer);
            push.meshletCullGroupDetailId = asSRV(queue, meshletCullGroupDetailBuffer);
            push.drawedMeshletCountId = asUAV(queue, drawMeshletCountBuffer);
            push.drawedMeshletCmdId = asUAV(queue, drawMeshletCmdBuffer);
            push.meshletCullCmdId = asSRV(queue, meshletCullCmdBuffer);

            auto computeShader = getContext().getShaderLibrary().getShader<GLTFBasePassMeshletCullCS>();
            addIndirectComputePass2(queue,
                "GLTFBasePass: MeshletLevelCulling",
                getContext().computePipe(computeShader, "GLTFBasePassPipe: MeshletLevelCulling"),
                push,
                meshletCullCmdBuffer);
        }

        // #3. Draw all visible meshlet.
        {
            GLTFDrawPushConsts push = pushTemplate;
            push.meshletCullGroupCountId = asSRV(queue, meshletCullGroupCountBuffer);
            push.meshletCullGroupDetailId = asSRV(queue, meshletCullGroupDetailBuffer);
            push.drawedMeshletCountId = asUAV(queue, drawMeshletCountBuffer);
            push.drawedMeshletCmdId = asUAV(queue, drawMeshletCmdBuffer);
            push.meshletCullCmdId = asSRV(queue, meshletCullCmdBuffer);

            RenderTargets RTs{ };
            RTs.RTs[0] = RenderTargetRT(gbuffers.color, ERenderTargetLoadStoreOp::Load_Store);
            RTs.depthStencil = DepthStencilRT(
                gbuffers.depthStencil,
                EDepthStencilOp::DepthWrite_StencilNop,
                ERenderTargetLoadStoreOp::Load_Store);

            auto vertexShader = getContext().getShaderLibrary().getShader<GLTFBasePassRenderingVS>();
            auto pixelShader = getContext().getShaderLibrary().getShader<GLTFBasePassRenderingPS>();

            auto pipeline = getContext().graphicsPipe(
                vertexShader, pixelShader,
                "GLTF BasePass",
                std::move(RTs.getRTsFormats()),
                RTs.getDepthStencilFormat(),
                RTs.getDepthStencilFormat());

            addIndirectDrawPass(
                queue,
                "GLTF Basepass: Raster",
                pipeline,
                RTs,
                drawMeshletCmdBuffer, 0, drawMeshletCountBuffer, 0,
                kMaxMeshletCount, sizeof(GLTFMeshDrawCmd), 
                [push](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
                {
                    pipe->pushConst(cmd, push);

                    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
                    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);

                    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
                    vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
                    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_GREATER_OR_EQUAL); 

                    VkColorComponentFlags colorWriteMask =
                        VK_COLOR_COMPONENT_R_BIT |
                        VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT;
                    vkCmdSetColorWriteMaskEXT(cmd, 0, 1, &colorWriteMask);
                });
        }
    }

}