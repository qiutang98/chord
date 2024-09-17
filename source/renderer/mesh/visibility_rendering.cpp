#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/visibility_buffer.hlsl>
#include <shader/instance_culling.hlsl>
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

class SV_bMaskedMaterial : SHADER_VARIANT_BOOL("DIM_MASKED_MATERIAL");
class SV_bTwoSide : SHADER_VARIANT_BOOL("DIM_TWO_SIDED");

class VisibilityBufferMS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);
    using Permutation = TShaderVariantVector<SV_bMaskedMaterial, SV_bTwoSide>;
};
IMPLEMENT_GLOBAL_SHADER(VisibilityBufferMS, "resource/shader/visibility_buffer.hlsl", "visibilityPassMS", EShaderStage::Mesh);

class VisibilityBufferPS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);
    using Permutation = TShaderVariantVector<SV_bMaskedMaterial, SV_bTwoSide>;
};
IMPLEMENT_GLOBAL_SHADER(VisibilityBufferPS, "resource/shader/visibility_buffer.hlsl", "visibilityPassPS", EShaderStage::Pixel);

static inline void renderVisibilityPipe(
    GLTFRenderContext& renderCtx,
    bool bMaskedMaterial,
    bool bTwoSide,
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

    VisibilityBufferMS::Permutation MSPermutation;
    MSPermutation.set<SV_bMaskedMaterial>(bMaskedMaterial);
    MSPermutation.set<SV_bTwoSide>(bTwoSide);
    auto meshShader = getContext().getShaderLibrary().getShader<VisibilityBufferMS>(MSPermutation);

    VisibilityBufferPS::Permutation PSPermutation;
    PSPermutation.set<SV_bMaskedMaterial>(bMaskedMaterial);
    PSPermutation.set<SV_bTwoSide>(bTwoSide);
    auto pixelShader = getContext().getShaderLibrary().getShader<VisibilityBufferPS>(PSPermutation);

    GraphicsPipelineRef pipeline = getContext().graphicsMeshShadingPipe(
        nullptr, // amplifyShader
        meshShader, 
        pixelShader,
        bMaskedMaterial ? "Visibility: Masked" : "Visibility",
        std::move(RTs.getRTsFormats()),
        RTs.getDepthStencilFormat(),
        RTs.getDepthStencilFormat());

    auto clusterCmdBuffer = indirectDispatchCmdFill("VisibilityCmd", renderCtx.queue, 1, countBuffer);

    addMeshIndirectDrawPass(
        queue,
        "GLTF Visibility: Raster",
        pipeline,
        RTs,
        clusterCmdBuffer, 0, sizeof(uint4), 1,
        [&](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
        {
            vkCmdSetCullMode(cmd, cullMode);
            pipe->pushConst(cmd, pushConst);

            // Visibility pass enable depth write and depth test.
            helper::enableDepthTestDepthWrite(cmd);
        });
}

static inline void renderVisibility(GLTFRenderContext& renderCtx, PoolBufferGPUOnlyRef inCmdBuffer, PoolBufferGPUOnlyRef inCountBuffer)
{
    auto& pool = getContext().getBufferPool();
    auto& queue = renderCtx.queue;

    auto& gbuffers = renderCtx.gbuffers;
    const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

    auto filterCmd = chord::detail::filterPipelineIndirectDispatchCmd(renderCtx, "Pipeline filter prepare.", inCountBuffer);

    for (uint alphaMode = 0; alphaMode <= 1; alphaMode++) // alpha mode == 3 meaning blend.
    {
        for (uint bTwoside = 0; bTwoside <= 1; bTwoside++)
        {
            // Fill cmd for indirect.
            auto [filteredCountBuffer, filteredCmdBuffer] = 
                chord::detail::filterPipeForVisibility(renderCtx, filterCmd, inCmdBuffer, inCountBuffer, alphaMode, bTwoside);
        

            // Now rendering.
            const bool bMasked = (alphaMode == 1);
            VkCullModeFlags cullMode = bTwoside ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            renderVisibilityPipe(renderCtx, bMasked, bTwoside, cullMode, filteredCmdBuffer, filteredCountBuffer);
        }
    }
}

bool chord::gltfVisibilityRenderingStage0(GLTFRenderContext& renderCtx, const HZBContext& hzbCtx)
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

        if (hzbCtx.isValid() && enableGLTFHZBCulling())
        {
            bShouldInvokeStage1 = true;

            chord::CountAndCmdBuffer countAndCmdBuffers;
            auto [countBufferStage1, cmdBufferStage1] =
                detail::hzbCulling(hzbCtx, renderCtx, true, renderCtx.postBasicCullingCtx.meshletCountBuffer, renderCtx.postBasicCullingCtx.meshletCmdBuffer, countAndCmdBuffers);

            renderVisibility(renderCtx, countAndCmdBuffers.second, countAndCmdBuffers.first);

            renderCtx.postBasicCullingCtx.meshletCmdBufferStage   = cmdBufferStage1;
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
    auto cmdBufferStage1   = renderCtx.postBasicCullingCtx.meshletCmdBufferStage;
    check(countBufferStage1 && cmdBufferStage1);

    auto& gbuffers = renderCtx.gbuffers;
    auto& queue = renderCtx.queue;

    chord::CountAndCmdBuffer countAndCmdBuffers;
    detail::hzbCulling(hzbCtx, renderCtx, false, countBufferStage1, cmdBufferStage1, countAndCmdBuffers);

    renderVisibility(renderCtx, countAndCmdBuffers.second, countAndCmdBuffers.first);
}