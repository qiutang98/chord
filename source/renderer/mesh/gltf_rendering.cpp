#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/gltf_basepass.hlsl>

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

    static uint32 sGLTFRenderingShaderDebugColor = 0;
    static AutoCVarRef cVarGLTFRenderingShaderDebugColor(
        "r.gltf.rendering.color",
        sGLTFRenderingShaderDebugColor,
        "Color"
    );

    static uint32 sGLTFRenderingShaderDebugPosition = 5;
    static AutoCVarRef cVarGLTFRenderingShaderDebugPosition(
        "r.gltf.rendering.position",
        sGLTFRenderingShaderDebugPosition,
        "Position"
    );
#if 0
    namespace GLTFRendering
    {
        class GLTFRenderingVS : public GlobalShader
        {
        public:
            DECLARE_SUPER_TYPE(GlobalShader);

        };
        IMPLEMENT_GLOBAL_SHADER(GLTFRenderingVS, "resource/shader/gltf_basepass.hlsl", "mainVS", EShaderStage::Vertex);

        class GLTFRenderingPS : public GlobalShader
        {
        public:
            DECLARE_SUPER_TYPE(GlobalShader);

        };
        IMPLEMENT_GLOBAL_SHADER(GLTFRenderingPS, "resource/shader/gltf_basepass.hlsl", "mainPS", EShaderStage::Pixel);
    }
#endif
    void chord::gltfBasePassRendering(
        graphics::GraphicsQueue& queue, 
        GBufferTextures& gbuffers, 
        uint32 cameraView,
        const GLTFRenderDescriptor& gltfRenderDescriptor)
    {
#if 0
        if (gltfRenderDescriptor.objectCount == 0)
        {
            return;
        }

        using namespace GLTFRendering;

        queue.checkRecording();

        RenderTargets RTs{ };
        RTs.RTs[0] = RenderTargetRT(gbuffers.color, ERenderTargetLoadStoreOp::Load_Store);
        RTs.depthStencil = DepthStencilRT(
            gbuffers.color, 
            EDepthStencilOp::DepthWrite_StencilNop,
            ERenderTargetLoadStoreOp::Load_Store);

        
        auto taskShader = nullptr;

        // Mesh shader
        auto meshShader = getContext().getShaderLibrary().getShader<GLTFRenderingMS>();

        // Pixel shader
        auto pixelShader = getContext().getShaderLibrary().getShader<GLTFRenderingPS>();

        auto pipeline = getContext().graphicsMeshShadingPipe(
            taskShader, meshShader, pixelShader,
            "GLTF BasePass",
            std::move(RTs.getRTsFormats()),
            RTs.getDepthStencilFormat(),
            RTs.getDepthStencilFormat());

        auto& cmd = queue.getActiveCmd()->commandBuffer;
        RTs.beginRendering(queue);
        {
            vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

            pipeline->bind(cmd);
            const auto& primitives = gltfRenderDescriptor.perframeCollect->gltfPrimitives;

            for (uint32 i = 0; i < gltfRenderDescriptor.objectCount; i++)
            {
                GLTFDrawPushConsts pushConst{ };
                pushConst.cameraViewId = cameraView;
                pushConst.objectId = i;
                pushConst.lod = 0;

                pipeline->pushConst(cmd, pushConst);

                vkCmdDrawMeshTasksEXT(cmd, 10240, 1, 1);
            }
        }
        RTs.endRendering(queue);
#endif
    }

}