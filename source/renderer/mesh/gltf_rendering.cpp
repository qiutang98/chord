#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/gltf.hlsl>

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

    namespace GLTFRendering
    {
        class SV_bDebugPrint  : SHADER_VARIANT_BOOL("DEBUG_PRINT");
        class SV_PositionMode : SHADER_VARIANT_SPARSE_INT("POSITION_MODE", 5, 9);
        class SV_ColorMode    : SHADER_VARIANT_RANGE_INT("COLOR_MODE", 0, 3);

        class GLTFRenderingAS : public GlobalShader
        {
        public:
            DECLARE_SUPER_TYPE(GlobalShader);
            using Permutation = TShaderVariantVector<SV_bDebugPrint>;
        };

        class GLTFRenderingMS : public GlobalShader
        {
        public:
            DECLARE_SUPER_TYPE(GlobalShader);
            using Permutation = TShaderVariantVector<SV_PositionMode, SV_ColorMode>;
        };

        IMPLEMENT_GLOBAL_SHADER(GLTFRenderingAS, "resource/shader/gltf.hlsl", "mainAS", EShaderStage::Amplify);
        IMPLEMENT_GLOBAL_SHADER(GLTFRenderingMS, "resource/shader/gltf.hlsl", "mainMS", EShaderStage::Mesh);

        PRIVATE_GLOBAL_SHADER(GLTFRenderingPS, "resource/shader/gltf.hlsl", "mainPS", EShaderStage::Pixel);
    }

    void chord::gltfBasePassRendering(
        graphics::GraphicsQueue& queue, 
        GBufferTextures& gbuffers, 
        uint32 cameraView)
    {
        using namespace GLTFRendering;

        queue.checkRecording();

        RenderTargets RTs{ };
        RTs.RTs[0] = RenderTargetRT(gbuffers.color, ERenderTargetLoadStoreOp::Load_Store);

        GLTFDrawPushConsts pushConst { };
        pushConst.cameraViewId = cameraView;

        GLTFRenderingAS::Permutation taskShaderPermutations;
        taskShaderPermutations.set<SV_bDebugPrint>(sGLTFRenderingShaderDebugMode);


        auto taskShader = getContext().getShaderLibrary().getShader<GLTFRenderingAS>(taskShaderPermutations);

        GLTFRenderingMS::Permutation meshShaderPermutations;
        meshShaderPermutations.set<SV_PositionMode>(sGLTFRenderingShaderDebugPosition);
        meshShaderPermutations.set<SV_ColorMode>(sGLTFRenderingShaderDebugColor);
        auto meshShader = getContext().getShaderLibrary().getShader<GLTFRenderingMS>(meshShaderPermutations);

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
            pipeline->pushConst(cmd, pushConst);

            vkCmdDrawMeshTasksEXT(cmd, 1, 1, 1);
        }
        RTs.endRendering(queue);
    }

}