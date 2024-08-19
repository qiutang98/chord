#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/visibility_buffer.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/postprocessing/postprocessing.h>

using namespace chord;
using namespace chord::graphics;

static uint32 sGLTFRenderingEnable = 1;
static AutoCVarRef cVarGLTFRenderingEnable(
    "r.gltf.rendering",
    sGLTFRenderingEnable,
    "Enable gltf rendering or not."
);

bool chord::shouldRenderGLTF(GLTFRenderContext& renderCtx)
{
    return (renderCtx.gltfObjectCount != 0) && (sGLTFRenderingEnable != 0);
}

static inline bool shouldUseMeshShader()
{
    return false;
}

class SV_bMaskedMaterial : SHADER_VARIANT_BOOL("DIM_MASKED_MATERIAL");

class VisibilityBufferVS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);
    using Permutation = TShaderVariantVector<SV_bMaskedMaterial>;
};
IMPLEMENT_GLOBAL_SHADER(VisibilityBufferVS, "resource/shader/visibility_buffer.hlsl", "visibilityPassVS", EShaderStage::Vertex);

class VisibilityBufferPS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);
    using Permutation = TShaderVariantVector<SV_bMaskedMaterial>;
};
IMPLEMENT_GLOBAL_SHADER(VisibilityBufferPS, "resource/shader/visibility_buffer.hlsl", "visibilityPassPS", EShaderStage::Pixel);


static inline void renderVisibilityPipe(
    GLTFRenderContext& renderCtx,
    bool bMaskedMaterial,
    VkCullModeFlags cullMode,
    PoolBufferGPUOnlyRef cmdBuffer,
    PoolBufferGPUOnlyRef countBuffer)
{
    auto& gbuffers = renderCtx.gbuffers;
    auto& queue = renderCtx.queue;
    const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

    VisibilityBufferPushConst pushConst{ };
    pushConst.cameraViewId = renderCtx.cameraView;
    pushConst.drawedMeshletCmdId = asSRV(queue, cmdBuffer);

    RenderTargets RTs{ };
    RTs.RTs[0] = RenderTargetRT(gbuffers.visibility, ERenderTargetLoadStoreOp::Load_Store);
    RTs.depthStencil = DepthStencilRT(
        renderCtx.gbuffers.depthStencil,
        EDepthStencilOp::DepthWrite_StencilWrite,
        ERenderTargetLoadStoreOp::Load_Store); // Already clear.

    VisibilityBufferPS::Permutation PSPermutation;
    PSPermutation.set<SV_bMaskedMaterial>(bMaskedMaterial);
    auto pixelShader = getContext().getShaderLibrary().getShader<VisibilityBufferPS>(PSPermutation);

    if (shouldUseMeshShader())
    {

    }
    else
    {
        VisibilityBufferVS::Permutation VSPermutation;
        VSPermutation.set<SV_bMaskedMaterial>(bMaskedMaterial);
        auto vertexShader = getContext().getShaderLibrary().getShader<VisibilityBufferVS>(VSPermutation);

        GraphicsPipelineRef pipeline = getContext().graphicsPipe(
            vertexShader, pixelShader,
            bMaskedMaterial ? "Visibility: Masked" : "Visibility",
            std::move(RTs.getRTsFormats()),
            RTs.getDepthStencilFormat(),
            RTs.getDepthStencilFormat());

        addIndirectDrawPass(
            queue,
            "GLTF Visibility: Raster",
            pipeline,
            RTs,
            cmdBuffer, 0, countBuffer, 0,
            lod0MeshletCount, sizeof(GLTFMeshletDrawCmd),
            [&](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
            {
                vkCmdSetCullMode(cmd, cullMode);
                pipe->pushConst(cmd, pushConst);

                // Visibility pass enable depth write and depth test.
                helper::enableDepthTestDepthWrite(cmd);
            });
    }
}

static inline void renderVisibility(GLTFRenderContext& renderCtx, PoolBufferGPUOnlyRef inCmdBuffer, PoolBufferGPUOnlyRef inCountBuffer)
{
    auto& gbuffers = renderCtx.gbuffers;
    auto& queue = renderCtx.queue;
    const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

    auto filterCmd = chord::detail::fillIndirectDispatchCmd(renderCtx, "Pipeline filter prepare.", inCountBuffer);

    for (uint alphaMode = 0; alphaMode <= 1; alphaMode++) // alpha mode == 3 meaning blend.
    {
        for (uint bTwoside = 0; bTwoside <= 1; bTwoside++)
        {
            // Fill cmd for indirect.
            auto [filteredCountBuffer, filteredCmdBuffer] = 
                chord::detail::filterPipeForVisibility(shouldUseMeshShader(), renderCtx, filterCmd, inCmdBuffer, inCountBuffer, alphaMode, bTwoside);
        

            // Now rendering.
            const bool bMasked = alphaMode == 1;
            VkCullModeFlags cullMode = bTwoside ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            renderVisibilityPipe(renderCtx, bMasked, cullMode, filteredCmdBuffer, filteredCountBuffer);
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

    {
        auto& queue = renderCtx.queue;
        auto& pool = getContext().getBufferPool();
        ScopePerframeMarker marker(queue, "Visibility Stage#0");

        if (renderCtx.history.hzbCtx.isValid())
        {
            bShouldInvokeStage1 = true;

            chord::detail::CountAndCmdBuffer countAndCmdBuffers;
            auto [countBufferStage1, cmdBufferStage1] =
                detail::hzbCulling(renderCtx, true, renderCtx.postBasicCullingCtx.meshletCountBuffer, renderCtx.postBasicCullingCtx.meshletCmdBuffer, countAndCmdBuffers);

            renderVisibility(renderCtx, countAndCmdBuffers.second, countAndCmdBuffers.first);

            renderCtx.postBasicCullingCtx.meshletCmdBufferStage = cmdBufferStage1;
            renderCtx.postBasicCullingCtx.meshletCountBufferStage = countBufferStage1;
        }
        else
        {
            renderVisibility(renderCtx, renderCtx.postBasicCullingCtx.meshletCmdBuffer, renderCtx.postBasicCullingCtx.meshletCountBuffer);
        }
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

    chord::detail::CountAndCmdBuffer countAndCmdBuffers;
    detail::hzbCulling(renderCtx, false, countBufferStage1, cmdBufferStage1, countAndCmdBuffers);

    renderVisibility(renderCtx, countAndCmdBuffers.second, countAndCmdBuffers.first);
}