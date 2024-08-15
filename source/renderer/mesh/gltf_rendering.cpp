#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/gltf_rendering.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/postprocessing/postprocessing.h>

namespace chord
{
    using namespace graphics;

    static uint32 sGLTFRenderingEnable = 1;
    static AutoCVarRef cVarGLTFRenderingEnable(
        "r.gltf.rendering",
        sGLTFRenderingEnable,
        "Enable gltf rendering or not."
    );

    static uint32 sGLTFRenderingEnableFrustumCulling = 1;
    static AutoCVarRef cVarGLTFRenderingEnableFrustumCulling(
        "r.gltf.rendering.frustumCulling",
        sGLTFRenderingEnableFrustumCulling,
        "Enable frustum culling or not."
    );

    static uint32 sGLTFRenderingEnableMeshletConeCulling = 1;
    static AutoCVarRef cVarGLTFRenderingEnableMeshletConeCulling(
        "r.gltf.rendering.meshletConeCulling",
        sGLTFRenderingEnableMeshletConeCulling,
        "Enable meshlet cone culling or not."
    );

    static uint32 sGLTFRenderingEnableHZBCulling = 1;
    static AutoCVarRef cVarGLTFRenderingEnableHZBCulling(
        "r.gltf.rendering.hzbCulling",
        sGLTFRenderingEnableHZBCulling,
        "Enable meshlet hzb culling or not."
    );

    static uint32 sGLTFRenderingEnableLOD = 1;
    static AutoCVarRef cVarGLTFRenderingEnableLOD(
        "r.gltf.rendering.lod",
        sGLTFRenderingEnableLOD,
        "Enable lod or not."
    );

    static uint32 sGLTFRenderingShaderDebugMode = 0;
    static AutoCVarRef cVarGLTFRenderingShaderDebugMode(
        "r.gltf.rendering.shaderDebugMode",
        sGLTFRenderingShaderDebugMode,
        "**** GLTF rendering shader debug mode ****"
        "  0. default state, do nothing."
        "  1. draw meshlet bounds box."
    );

    static inline uint32 getGLTFRenderingSwitchFlags()
    {
        uint32 result = 0;
        if (sGLTFRenderingEnableLOD != 0) { result = shaderSetFlag(result, kLODEnableBit); }
        if (sGLTFRenderingEnableFrustumCulling != 0) { result = shaderSetFlag(result, kFrustumCullingEnableBit); }
        if (sGLTFRenderingEnableHZBCulling != 0) { result = shaderSetFlag(result, kHZBCullingEnableBit); }
        if (sGLTFRenderingEnableMeshletConeCulling != 0) { result = shaderSetFlag(result, kMeshletConeCullEnableBit); }
        return result;
    }

    static inline bool shouldPrintDebugBox()
    {
        return sGLTFRenderingShaderDebugMode == 1;
    }

    static inline bool shouldRenderGLTF(GLTFRenderContext& renderCtx)
    {
        return (renderCtx.gltfObjectCount != 0) 
            && (sGLTFRenderingEnable != 0);
    }

    static inline GLTFDrawPushConsts getGLTFDrawPushConsts(GLTFRenderContext& renderCtx)
    {
        GLTFDrawPushConsts pushTemplate { };
        pushTemplate.cameraViewId = renderCtx.cameraView;
        pushTemplate.debugFlags = sGLTFRenderingShaderDebugMode;
        pushTemplate.switchFlags = getGLTFRenderingSwitchFlags();

        return pushTemplate;
    }

    PRIVATE_GLOBAL_SHADER(GLTFRenderingPerObjectCullCS, "resource/shader/gltf_rendering.hlsl", "perobjectCullingCS", EShaderStage::Compute);
    PRIVATE_GLOBAL_SHADER(GLTFRenderingFillMeshletCullCmdCS, "resource/shader/gltf_rendering.hlsl", "fillMeshletCullCmdCS", EShaderStage::Compute);
    PRIVATE_GLOBAL_SHADER(GLTFRenderingMeshletCullCS, "resource/shader/gltf_rendering.hlsl", "meshletCullingCS", EShaderStage::Compute);

    void gltfRenderingBasicCulling(GLTFRenderContext& ctx)
    {
        auto& queue = ctx.queue;
        auto& gbuffers = ctx.gbuffers;
        uint32 cameraView = ctx.cameraView;
        const uint objectCount = ctx.gltfObjectCount;
        const uint maxMeshletCount = ctx.perframeCollect->gltfLod0MeshletCount;

        ScopePerframeMarker marker(queue, "GLTF: BasicCulling");

        check(objectCount == ctx.perframeCollect->gltfPrimitives.size());
        check(maxMeshletCount > 0);
        queue.checkRecording();

        GLTFDrawPushConsts pushTemplate { };
        pushTemplate.cameraViewId = cameraView;
        pushTemplate.debugFlags = sGLTFRenderingShaderDebugMode;
        pushTemplate.switchFlags = getGLTFRenderingSwitchFlags();

        // #0. Object level culling.
        auto meshletCullGroupCountBuffer = getContext().getBufferPool().createGPUOnly(
            "meshletCullGroupCountBuffer",
            sizeof(uint),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto meshletCullGroupDetailBuffer = getContext().getBufferPool().createGPUOnly(
            "meshletCullGroupDetailBuffer",
            sizeof(uvec2) * maxMeshletCount,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        queue.clearUAV(meshletCullGroupCountBuffer);
        {
            GLTFDrawPushConsts push = pushTemplate;
            push.meshletCullGroupCountId = asUAV(queue, meshletCullGroupCountBuffer);
            push.meshletCullGroupDetailId = asUAV(queue, meshletCullGroupDetailBuffer);

            auto computeShader = getContext().getShaderLibrary().getShader<GLTFRenderingPerObjectCullCS>();
            addComputePass2(queue,
                "GLTFBasePass: ObjectCull",
                getContext().computePipe(computeShader, "GLTFBasePassPipe: ObjectCull"),
                push,
                math::uvec3(divideRoundingUp(objectCount, 64U), 1, 1));
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
            auto computeShader = getContext().getShaderLibrary().getShader<GLTFRenderingFillMeshletCullCmdCS>();

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
            sizeof(GLTFMeshDrawCmd) * maxMeshletCount,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        queue.clearUAV(drawMeshletCountBuffer);
        {
            GLTFDrawPushConsts push = pushTemplate;

            push.meshletCullGroupCountId  = asSRV(queue, meshletCullGroupCountBuffer);
            push.meshletCullGroupDetailId = asSRV(queue, meshletCullGroupDetailBuffer);
            push.drawedMeshletCountId     = asUAV(queue, drawMeshletCountBuffer);
            push.drawedMeshletCmdId       = asUAV(queue, drawMeshletCmdBuffer);
            push.meshletCullCmdId         = asSRV(queue, meshletCullCmdBuffer);

            auto computeShader = getContext().getShaderLibrary().getShader<GLTFRenderingMeshletCullCS>();
            addIndirectComputePass2(queue,
                "GLTFBasePass: MeshletLevelCulling",
                getContext().computePipe(computeShader, "GLTFBasePassPipe: MeshletLevelCulling"),
                push,
                meshletCullCmdBuffer);
        }

        ctx.postBasicCullingCtx.meshletCmdBuffer = drawMeshletCmdBuffer;
        ctx.postBasicCullingCtx.meshletCountBuffer = drawMeshletCountBuffer;
    }

    class GLTFVisibilityVS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);

        class SV_bMaskedMaterial : SHADER_VARIANT_BOOL("DIM_MASKED_MATERIAL");
        using Permutation = TShaderVariantVector<SV_bMaskedMaterial>;

        static void modifyCompileEnvironment(ShaderCompileEnvironment& o, int32 PermutationId) 
        { 
            o.enableDebugSource();
        }
    };
    IMPLEMENT_GLOBAL_SHADER(GLTFVisibilityVS, "resource/shader/gltf_rendering.hlsl", "visibilityPassVS", EShaderStage::Vertex);

    class GLTFVisibilityPS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);

        class SV_bMaskedMaterial : SHADER_VARIANT_BOOL("DIM_MASKED_MATERIAL");
        using Permutation = TShaderVariantVector<SV_bMaskedMaterial>;
    };
    IMPLEMENT_GLOBAL_SHADER(GLTFVisibilityPS, "resource/shader/gltf_rendering.hlsl", "visibilityPassPS", EShaderStage::Pixel);

    PRIVATE_GLOBAL_SHADER(GLTFRenderingFillHZBCullCmdCS, "resource/shader/gltf_rendering.hlsl", "fillHZBCullParamCS", EShaderStage::Compute);
    PRIVATE_GLOBAL_SHADER(GLTFRenderingFillPipelineDrawParamCS, "resource/shader/gltf_rendering.hlsl", "fillPipelineDrawParamCS", EShaderStage::Compute);

    class GLTFHZBCullCS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);

        class SV_bFirstStage : SHADER_VARIANT_BOOL("DIM_HZB_CULLING_PHASE_0");
        class SV_bPrintDebugBox : SHADER_VARIANT_BOOL("DIM_PRINT_DEBUG_BOX");
        using Permutation = TShaderVariantVector<SV_bFirstStage, SV_bPrintDebugBox>;
    };
    IMPLEMENT_GLOBAL_SHADER(GLTFHZBCullCS, "resource/shader/gltf_rendering.hlsl", "HZBCullingCS", EShaderStage::Compute);

    static inline auto fillIndirectDispatchCmd(GLTFRenderContext& renderCtx, const std::string& name, PoolBufferGPUOnlyRef countBuffer)
    {
        auto meshletCullCmdBuffer = getContext().getBufferPool().createGPUOnly(
            name,
            sizeof(uvec4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        {
            GLTFDrawPushConsts push = getGLTFDrawPushConsts(renderCtx);
            push.drawedMeshletCountId = asSRV(renderCtx.queue, countBuffer);
            push.meshletCullCmdId = asUAV(renderCtx.queue, meshletCullCmdBuffer);
            auto computeShader = getContext().getShaderLibrary().getShader<GLTFRenderingFillHZBCullCmdCS>();

            addComputePass2(renderCtx.queue,
                name,
                getContext().computePipe(computeShader, name),
                push,
                math::uvec3(1, 1, 1));
        }

        return meshletCullCmdBuffer;
    };

    static inline void fillHZBPushParam(GLTFRenderContext& renderCtx, GLTFDrawPushConsts& push, const HZBContext& inHzb)
    {
        push.hzb = asSRV(renderCtx.queue, inHzb.minHZB); //
        push.hzbMipCount = inHzb.mipmapLevelCount;

        push.uv2HzbX = float(inHzb.mip0ValidArea.x) / float(inHzb.dimension.x);
        push.uv2HzbY = float(inHzb.mip0ValidArea.y) / float(inHzb.dimension.y);
        push.hzbMip0Width = inHzb.dimension.x;
        push.hzbMip0Height = inHzb.dimension.y;
    };

    static inline void gltfRenderVisibilityPipe(GLTFRenderContext& renderCtx, bool bMaskedMaterial, VkCullModeFlags cullMode, PoolBufferGPUOnlyRef cmdBuffer, PoolBufferGPUOnlyRef countBuffer)
    {
        auto& gbuffers = renderCtx.gbuffers;
        auto& queue = renderCtx.queue;
        const uint maxMeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

        GLTFDrawPushConsts push = getGLTFDrawPushConsts(renderCtx);

        // 
        push.drawedMeshletCountId = asSRV(queue, countBuffer);
        push.drawedMeshletCmdId = asSRV(queue, cmdBuffer);

        RenderTargets RTs{ };
        RTs.RTs[0] = RenderTargetRT(gbuffers.visibility, ERenderTargetLoadStoreOp::Load_Store);
        RTs.depthStencil = DepthStencilRT(
            renderCtx.gbuffers.depthStencil,
            EDepthStencilOp::DepthWrite_StencilWrite,
            ERenderTargetLoadStoreOp::Load_Store); // Already clear.

        GLTFVisibilityVS::Permutation VSPermutation;
        VSPermutation.set<GLTFVisibilityVS::SV_bMaskedMaterial>(bMaskedMaterial);
        auto vertexShader = getContext().getShaderLibrary().getShader<GLTFVisibilityVS>(VSPermutation);

        GLTFVisibilityPS::Permutation PSPermutation;
        PSPermutation.set<GLTFVisibilityPS::SV_bMaskedMaterial>(bMaskedMaterial);
        auto pixelShader = getContext().getShaderLibrary().getShader<GLTFVisibilityPS>(PSPermutation);

        GraphicsPipelineRef pipeline = getContext().graphicsPipe(
            vertexShader, pixelShader,
            bMaskedMaterial ? "GLTF Visibility - Masked" : "GLTF Visibility",
            std::move(RTs.getRTsFormats()),
            RTs.getDepthStencilFormat(),
            RTs.getDepthStencilFormat());

        addIndirectDrawPass(
            queue,
            "GLTF Visibility: Raster",
            pipeline,
            RTs,
            cmdBuffer, 0, countBuffer, 0,
            maxMeshletCount, sizeof(GLTFMeshDrawCmd),
            [&](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
            {
                vkCmdSetCullMode(cmd, cullMode);
                pipe->pushConst(cmd, push);

                // Visibility pass enable depth write and depth test.
                helper::enableDepthTestDepthWrite(cmd);
            });
    }

    static inline void gltfRenderVisibility(GLTFRenderContext& renderCtx, PoolBufferGPUOnlyRef cmdBuffer, PoolBufferGPUOnlyRef countBuffer)
    {
        auto& gbuffers = renderCtx.gbuffers;
        auto& queue = renderCtx.queue;
        const uint maxMeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

        auto filterCmd = fillIndirectDispatchCmd(renderCtx, "Pipeline filter prepare.", countBuffer);
        for (uint alphaMode = 0; alphaMode <= 1; alphaMode++) // alpha mode == 3 meaning blend.
        {
            for (uint bTwoside = 0; bTwoside <= 1; bTwoside++)
            {
                auto countBufferStage1 = getContext().getBufferPool().createGPUOnly(
                    "CountBufferStage#1",
                    sizeof(uint),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
                auto cmdBufferStage1 = getContext().getBufferPool().createGPUOnly(
                    "CmdBufferStage#1",
                    sizeof(GLTFMeshDrawCmd) * maxMeshletCount,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

                queue.clearUAV(countBufferStage1);

                GLTFDrawPushConsts pushCullTemplate = getGLTFDrawPushConsts(renderCtx);
                pushCullTemplate.drawedMeshletCountId = asSRV(queue, countBuffer);
                pushCullTemplate.drawedMeshletCmdId = asSRV(queue, cmdBuffer);
                pushCullTemplate.drawedMeshletCountId_1 = asUAV(queue, countBufferStage1);
                pushCullTemplate.drawedMeshletCmdId_1 = asUAV(queue, cmdBufferStage1);

                pushCullTemplate.targetTwoSide = bTwoside;
                pushCullTemplate.targetAlphaMode = alphaMode;

                addIndirectComputePass2(queue,
                    "GLTFPipeline: Filter",
                    getContext().computePipe(getContext().getShaderLibrary().getShader<GLTFRenderingFillPipelineDrawParamCS>(), "GLTFPipeline: Filter"),
                    pushCullTemplate,
                    filterCmd);

                const bool bMasked = alphaMode == 1;
                VkCullModeFlags cullMode = bTwoside ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

                gltfRenderVisibilityPipe(renderCtx, bMasked, cullMode, cmdBufferStage1, countBufferStage1);
            }
        }
    }

    bool chord::gltfVisibilityRenderingStage0(GLTFRenderContext& renderCtx)
    {
        bool bShouldInvokeStage1 = false;
        if (!shouldRenderGLTF(renderCtx))
        {
            return bShouldInvokeStage1;
        }

        // Do basic culling of current frame.
        gltfRenderingBasicCulling(renderCtx);

        auto& queue = renderCtx.queue;
        ScopePerframeMarker marker(queue, "GLTF Prepass");

        if (renderCtx.history.hzbCtx.isValid())
        {
            bShouldInvokeStage1 = true;

            // Stage #0.
            PoolBufferGPUOnlyRef countBufferStage1;
            PoolBufferGPUOnlyRef cmdBufferStage1;
            {
                const uint maxMeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

                auto meshletCullCmdBuffer = fillIndirectDispatchCmd(renderCtx, "meshletCullCmdBuffer Stage#0", renderCtx.postBasicCullingCtx.meshletCountBuffer);
                auto countBuffer = getContext().getBufferPool().createGPUOnly(
                    "CountBufferStage#0",
                    sizeof(uint),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
                auto drawMeshletCmdBuffer = getContext().getBufferPool().createGPUOnly(
                    "CmdBufferStage#0",
                    sizeof(GLTFMeshDrawCmd) * maxMeshletCount,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

                countBufferStage1 = getContext().getBufferPool().createGPUOnly(
                    "CountBufferStage#1",
                    sizeof(uint),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
                cmdBufferStage1 = getContext().getBufferPool().createGPUOnly(
                    "CmdBufferStage#1",
                    sizeof(GLTFMeshDrawCmd) * maxMeshletCount,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

                queue.clearUAV(countBuffer);
                queue.clearUAV(countBufferStage1);

                GLTFDrawPushConsts pushCullTemplate = getGLTFDrawPushConsts(renderCtx);
                fillHZBPushParam(renderCtx, pushCullTemplate, renderCtx.history.hzbCtx);

                pushCullTemplate.drawedMeshletCountId = asSRV(queue, renderCtx.postBasicCullingCtx.meshletCountBuffer);
                pushCullTemplate.drawedMeshletCmdId = asSRV(queue, renderCtx.postBasicCullingCtx.meshletCmdBuffer);
                pushCullTemplate.drawedMeshletCountId_1 = asUAV(queue, countBuffer);
                pushCullTemplate.drawedMeshletCmdId_1 = asUAV(queue, drawMeshletCmdBuffer);
                pushCullTemplate.drawedMeshletCountId_2 = asUAV(queue, countBufferStage1);
                pushCullTemplate.drawedMeshletCmdId_2 = asUAV(queue, cmdBufferStage1);

                GLTFHZBCullCS::Permutation permutation;
                permutation.set<GLTFHZBCullCS::SV_bFirstStage>(true);
                permutation.set<GLTFHZBCullCS::SV_bPrintDebugBox>(shouldPrintDebugBox());
                auto computeShader = getContext().getShaderLibrary().getShader<GLTFHZBCullCS>(permutation);  

                addIndirectComputePass2(queue,
                    "GLTFBasePass: MeshletLevelCulling",
                    getContext().computePipe(computeShader, "GLTFBasePassPipe: MeshletLevelCulling"),
                    pushCullTemplate,
                    meshletCullCmdBuffer);

                gltfRenderVisibility(renderCtx, drawMeshletCmdBuffer, countBuffer);
            }

            renderCtx.postBasicCullingCtx.meshletCmdBufferStage = cmdBufferStage1;
            renderCtx.postBasicCullingCtx.meshletCountBufferStage = countBufferStage1;
        }
        else  
        {
            gltfRenderVisibility(renderCtx, renderCtx.postBasicCullingCtx.meshletCmdBuffer, renderCtx.postBasicCullingCtx.meshletCountBuffer);
        }

        return bShouldInvokeStage1;
    }

    void chord::gltfVisibilityRenderingStage1(GLTFRenderContext& renderCtx, const HZBContext& hzbCtx)
    {
        auto countBufferStage1 = renderCtx.postBasicCullingCtx.meshletCountBufferStage;
        auto cmdBufferStage1 = renderCtx.postBasicCullingCtx.meshletCmdBufferStage;
        check(countBufferStage1 && cmdBufferStage1);

        auto& gbuffers = renderCtx.gbuffers;
        auto& queue = renderCtx.queue;
        const uint maxMeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

        // Stage #1.
        auto meshletCullCmdBuffer = fillIndirectDispatchCmd(renderCtx, "meshletCullCmdBuffer Stage#1", countBufferStage1);

        auto countBuffer = getContext().getBufferPool().createGPUOnly(
            "CountBufferStage#1 drawCount",
            sizeof(uint),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto drawMeshletCmdBuffer = getContext().getBufferPool().createGPUOnly(
            "CmdBufferStage#1 drawcmd",
            sizeof(GLTFMeshDrawCmd) * maxMeshletCount,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        queue.clearUAV(countBuffer);

        GLTFDrawPushConsts push = getGLTFDrawPushConsts(renderCtx);
        fillHZBPushParam(renderCtx, push, hzbCtx);

        push.drawedMeshletCountId = asSRV(queue, countBufferStage1);
        push.drawedMeshletCmdId = asSRV(queue, cmdBufferStage1);
        push.drawedMeshletCountId_1 = asUAV(queue, countBuffer);
        push.drawedMeshletCmdId_1 = asUAV(queue, drawMeshletCmdBuffer);

        GLTFHZBCullCS::Permutation permutation;
        permutation.set<GLTFHZBCullCS::SV_bFirstStage>(false);
        permutation.set<GLTFHZBCullCS::SV_bPrintDebugBox>(shouldPrintDebugBox());
        auto computeShader = getContext().getShaderLibrary().getShader<GLTFHZBCullCS>(permutation);

        addIndirectComputePass2(queue,
            "GLTFBasePass: MeshletLevelCulling2",
            getContext().computePipe(computeShader, "GLTFBasePassPipe: MeshletLevelCulling2"),
            push,
            meshletCullCmdBuffer);

        gltfRenderVisibility(renderCtx, drawMeshletCmdBuffer, countBuffer);
    }
}