#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/mesh_raster.hlsl>
#include <shader/instance_culling.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/postprocessing/postprocessing.h>
#include <scene/system/shadow.h>
#include <renderer/renderer.h>

using namespace chord;
using namespace chord::graphics;

static uint32 sGLTFRenderingEnable = 1;
static AutoCVarRef cVarGLTFRenderingEnable(
    "r.gltf.rendering",
    sGLTFRenderingEnable,
    "Enable gltf rendering or not."
);

static uint32 sShadowHZBCullingEnable = 1;
static AutoCVarRef cVarShadowHZBCullingEnable(
    "r.shadow.hzbCulling",
    sShadowHZBCullingEnable,
    "Enable hzb culling for shadow or not."
);

bool chord::shouldRenderGLTF(const GLTFRenderContext& renderCtx)
{
    return (renderCtx.gltfObjectCount != 0) && (sGLTFRenderingEnable != 0);
}

class SV_bMaskedMaterial : SHADER_VARIANT_BOOL("DIM_MASKED_MATERIAL");
class SV_bTwoSide        : SHADER_VARIANT_BOOL("DIM_TWO_SIDED");
class SV_PassType        : SHADER_VARIANT_SPARSE_INT("DIM_PASS_TYPE", PASS_TYPE_CLUSTER, PASS_TYPE_DEPTH);

class MeshRasterPassMS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);
    using Permutation = TShaderVariantVector<SV_bMaskedMaterial, SV_bTwoSide, SV_PassType>;
};
IMPLEMENT_GLOBAL_SHADER(MeshRasterPassMS, "resource/shader/mesh_raster.hlsl", "meshRasterPassMS", EShaderStage::Mesh);

class MeshRasterPassPS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);
    using Permutation = TShaderVariantVector<SV_bMaskedMaterial, SV_bTwoSide, SV_PassType>;
};
IMPLEMENT_GLOBAL_SHADER(MeshRasterPassPS, "resource/shader/mesh_raster.hlsl", "meshRasterPassPS", EShaderStage::Pixel);

static inline void renderMeshRasterPipe(
    graphics::GraphicsQueue& queue,
    RenderTargets RTs,
    const GLTFRenderContext& renderCtx,
    int passType,
    bool bMaskedMaterial,
    bool bTwoSide,
    bool bDepthClamped,
    float depthBiasConst,
    float depthBiasSlope,
    uint instanceViewId,
    uint instanceViewOffset,
    VkCullModeFlags cullMode,
    PoolBufferGPUOnlyRef cmdBuffer,
    PoolBufferGPUOnlyRef countBuffer)
{
    const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

    MeshRasterPushConst pushConst{ };
    pushConst.cameraViewId = renderCtx.cameraView;
    pushConst.drawedMeshletCmdId = asSRV(queue, cmdBuffer);
    pushConst.instanceViewId = instanceViewId;
    pushConst.instanceViewOffset = instanceViewOffset;

    bool bExistPixelShader = true;
    if (passType == PASS_TYPE_DEPTH)
    {
        bExistPixelShader = bMaskedMaterial;
    }

    MeshRasterPassMS::Permutation MSPermutation;
    MSPermutation.set<SV_bMaskedMaterial>(bMaskedMaterial);
    MSPermutation.set<SV_bTwoSide>(bTwoSide);
    MSPermutation.set<SV_PassType>(passType);
    auto meshShader = getContext().getShaderLibrary().getShader<MeshRasterPassMS>(MSPermutation);



    ShaderModuleRef pixelShader = nullptr;
    if (bExistPixelShader)
    {
        MeshRasterPassPS::Permutation PSPermutation;
        PSPermutation.set<SV_bMaskedMaterial>(bMaskedMaterial);
        PSPermutation.set<SV_bTwoSide>(bTwoSide);
        PSPermutation.set<SV_PassType>(passType);
        pixelShader = getContext().getShaderLibrary().getShader<MeshRasterPassPS>(PSPermutation);
    }

    GraphicsPipelineRef pipeline = getContext().graphicsMeshShadingPipe(
        nullptr, // amplifyShader
        meshShader, 
        pixelShader,
        bMaskedMaterial ? "Raster: Masked" : "Raster: NonMask",
        std::move(RTs.getRTsFormats()),
        RTs.getDepthFormat(),
        RTs.getStencilFormat());

    auto clusterCmdBuffer = indirectDispatchCmdFill("MeshRasterCmd", queue, 1, countBuffer);

    addMeshIndirectDrawPass(
        queue,
        "GLTF: Raster",
        pipeline,
        RTs,
        clusterCmdBuffer, 0, sizeof(uint4), 1,
        [&](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
        {
            vkCmdSetCullMode(cmd, cullMode);
            pipe->pushConst(cmd, pushConst);

            // Mesh raster pass enable depth write and depth test.
            helper::enableDepthTestDepthWrite(cmd);

            if (bDepthClamped)
            {
                vkCmdSetDepthClampEnableEXT(cmd, VK_TRUE);
            }

            if (depthBiasConst != 0.0f || depthBiasSlope != 0.0f)
            {
                vkCmdSetDepthBias(cmd, depthBiasConst, 0.0f, depthBiasSlope);
            }
        });
}

static inline void renderMeshDepth(
    graphics::GraphicsQueue& queue,
    RenderTargets RTs,
    const GLTFRenderContext& renderCtx,
    int passType,
    bool bDepthClamped,
    float depthBiasConst,
    float depthBiasSlope,
    uint instanceViewId,
    uint instanceViewOffset,
    PoolBufferGPUOnlyRef inCmdBuffer,
    PoolBufferGPUOnlyRef inCountBuffer)
{
    auto& pool = getContext().getBufferPool();
    const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

    auto filterCmd = chord::detail::filterPipelineIndirectDispatchCmd(queue, renderCtx, "Pipeline filter prepare.", inCountBuffer);

    for (uint alphaMode = 0; alphaMode <= 1; alphaMode++) // alpha mode == 3 meaning blend.
    {
        // Fill cmd for indirect, when render depth only pass, don't care two side state.
        auto [filteredCountBuffer, filteredCmdBuffer] =
            chord::detail::filterPipeForVisibility(queue, renderCtx, filterCmd, inCmdBuffer, inCountBuffer, alphaMode, detail::kNoCareTwoSideFlag);

        // Now rendering.
        const bool bMasked = (alphaMode == 1);

        // Always two side rendering.
        const bool bTwoSide = true; 
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
        renderMeshRasterPipe(
            queue, 
            RTs, 
            renderCtx, 
            passType, 
            bMasked, 
            bTwoSide, 
            bDepthClamped, 
            depthBiasConst, 
            depthBiasSlope, 
            instanceViewId, 
            instanceViewOffset, 
            cullMode, 
            filteredCmdBuffer, 
            filteredCountBuffer);
    }
}

static inline void renderMesh(
    graphics::GraphicsQueue& queue,
    RenderTargets RTs,
    const GLTFRenderContext& renderCtx, 
    const HZBContext& hzbCtx,
    int passType,
    uint instanceViewId,
    uint instanceViewOffset,
    PoolBufferGPUOnlyRef inCmdBuffer, 
    PoolBufferGPUOnlyRef inCountBuffer)
{
    auto& pool = getContext().getBufferPool();
    const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

    auto filterCmd = chord::detail::filterPipelineIndirectDispatchCmd(queue, renderCtx, "Pipeline filter prepare.", inCountBuffer);

    for (uint alphaMode = 0; alphaMode <= 1; alphaMode++) // alpha mode == 3 meaning blend.
    {
        for (uint bTwoside = 0; bTwoside <= 1; bTwoside++)
        {
            // Fill cmd for indirect.
            auto [filteredCountBuffer, filteredCmdBuffer] = 
                chord::detail::filterPipeForVisibility(queue, renderCtx, filterCmd, inCmdBuffer, inCountBuffer, alphaMode, bTwoside);
        

            // Now rendering.
            const bool bMasked = (alphaMode == 1);
            VkCullModeFlags cullMode = bTwoside ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            const bool bDepthClamped = false;
            renderMeshRasterPipe(
                queue, 
                RTs, 
                renderCtx, 
                passType, 
                bMasked, 
                bTwoside, 
                bDepthClamped, 
                0.0f,  // Depth bias
                0.0f,  // Depth slope bias
                instanceViewId, 
                instanceViewOffset, 
                cullMode, 
                filteredCmdBuffer, 
                filteredCountBuffer);
        }
    }
}

static inline RenderTargets getVisibilityRTs(const GBufferTextures& gbuffers)
{
    RenderTargets RTs{ };
    RTs.RTs[0] = RenderTargetRT(gbuffers.visibility, ERenderTargetLoadStoreOp::Load_Store);
    RTs.depthStencil = DepthStencilRT(
        gbuffers.depthStencil,
        EDepthStencilOp::DepthWrite_StencilWrite,
        ERenderTargetLoadStoreOp::Load_Store); // Already clear.

    return RTs;
}

bool chord::gltfVisibilityRenderingStage0(
    graphics::GraphicsQueue& queue,
    const GBufferTextures& gbuffers,
    const GLTFRenderContext& renderCtx, 
    const HZBContext& hzbCtx, 
    uint instanceViewId,
    uint instanceViewOffset,
    CountAndCmdBuffer inCountAndCmdBuffer,
    CountAndCmdBuffer& outCountAndCmdBuffer)
{
    bool bShouldInvokeStage1 = false;
    outCountAndCmdBuffer = { nullptr, nullptr };

    if (!shouldRenderGLTF(renderCtx))
    {
        return bShouldInvokeStage1;
    }

    {
        auto& pool = getContext().getBufferPool();
        ScopePerframeMarker marker(queue, "Visibility Stage#0");

        if (hzbCtx.isValid() && enableGLTFHZBCulling())
        {
            bShouldInvokeStage1 = true;

            chord::CountAndCmdBuffer countAndCmdBuffers;
            auto [countBufferStage1, cmdBufferStage1] =
                detail::hzbCulling(queue, hzbCtx, renderCtx, true, inCountAndCmdBuffer.first, inCountAndCmdBuffer.second, countAndCmdBuffers);

            renderMesh(queue, getVisibilityRTs(gbuffers), renderCtx, {}, PASS_TYPE_CLUSTER, instanceViewId, instanceViewOffset, countAndCmdBuffers.second, countAndCmdBuffers.first);

            outCountAndCmdBuffer = { countBufferStage1, cmdBufferStage1 };
        }
        else
        {
            renderMesh(queue, getVisibilityRTs(gbuffers), renderCtx, {}, PASS_TYPE_CLUSTER, instanceViewId, instanceViewOffset, inCountAndCmdBuffer.second, inCountAndCmdBuffer.first);
        }
    }

    return bShouldInvokeStage1;
}

void chord::gltfVisibilityRenderingStage1(
    graphics::GraphicsQueue& queue, 
    const GBufferTextures& gbuffers,
    const GLTFRenderContext& renderCtx, 
    const HZBContext& hzbCtx, 
    uint instanceViewId,
    uint instanceViewOffset,
    CountAndCmdBuffer inCountAndCmdBuffer)
{
    chord::CountAndCmdBuffer countAndCmdBuffers;
    detail::hzbCulling(queue, hzbCtx, renderCtx, false, inCountAndCmdBuffer.first, inCountAndCmdBuffer.second, countAndCmdBuffers);

    renderMesh(queue, getVisibilityRTs(gbuffers), renderCtx, hzbCtx, PASS_TYPE_CLUSTER, instanceViewId, instanceViewOffset, countAndCmdBuffers.second, countAndCmdBuffers.first);
}

CascadeShadowContext chord::renderShadow(
    graphics::CommandList& cmd, 
    graphics::GraphicsQueue& queue, 
    const GLTFRenderContext& renderCtx,
    const PerframeCameraView& cameraView,
    const CascadeShadowHistory& cascadeHistory,
    const std::vector<CascadeInfo>& cacsadeInfos,
    const ShadowConfig& config, 
    const ApplicationTickData& tickData, 
    const ICamera& camera, 
    const float3& lightDirection)
{
    if (!shouldRenderGLTF(renderCtx))
    {
        return { };
    }

    CascadeShadowContext resultCtx(config.cascadeConfig);
    resultCtx.direction = math::normalize(lightDirection);
    resultCtx.bDirectionChange = (cascadeHistory.direction != lightDirection);

    const uint realtimeCount = config.cascadeConfig.realtimeCascadeCount;

    check(realtimeCount <= cacsadeInfos.size());
    const uint intervalUpdatePeriod = cacsadeInfos.size() - realtimeCount;

    // Only valid if direction is same.
    const bool bCacheValid =
        !cascadeHistory.depths.empty()              &&
        cascadeHistory.direction == lightDirection  &&
        cascadeHistory.config    == config.cascadeConfig;
    
    resultCtx.viewInfos.resize(cacsadeInfos.size());
    resultCtx.depths.resize(cacsadeInfos.size());

    auto updateCascade = [&](int32 cascadeId)
    {
        // View infos.
        {
            const auto& cascade = cacsadeInfos[cascadeId];
            for (uint i = 0; i < 6; i++)
            {
                resultCtx.viewInfos[cascadeId].frustumPlanesRS[i] = cascade.frustumPlanes[i];
            }
            resultCtx.viewInfos[cascadeId].translatedWorldToClip = cascade.viewProjectMatrix;
            resultCtx.viewInfos[cascadeId].clipToTranslatedWorld = math::inverse(cascade.viewProjectMatrix);
            resultCtx.viewInfos[cascadeId].orthoDepthConvertToView = cascade.orthoDepthConvertToView;
            resultCtx.viewInfos[cascadeId].cameraWorldPos = cameraView.cameraWorldPos;
            resultCtx.viewInfos[cascadeId].renderDimension.x = (float)config.cascadeConfig.cascadeDim;
            resultCtx.viewInfos[cascadeId].renderDimension.y = (float)config.cascadeConfig.cascadeDim;
            resultCtx.viewInfos[cascadeId].renderDimension.z = 1.0f / (float)config.cascadeConfig.cascadeDim;
            resultCtx.viewInfos[cascadeId].renderDimension.w = 1.0f / (float)config.cascadeConfig.cascadeDim;
        }

        if (bCacheValid)
        {
            resultCtx.depths[cascadeId] = cascadeHistory.depths[cascadeId];
        }
        else
        {
            const uint cascsadeDim = config.cascadeConfig.cascadeDim;
            const VkImageUsageFlags imageUsage =
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            std::string name = std::format("CascadeShadowDepth #{}", cascadeId);
            resultCtx.depths[cascadeId] = getContext().getTexturePool().create(
                name,
                cascsadeDim,
                cascsadeDim,
                VK_FORMAT_D32_SFLOAT,
                imageUsage);
        }
    };

    auto isCascadeCacheValid = [=](int32 cascadeId) -> bool
    {
        if (!bCacheValid || cascadeId < realtimeCount)
        {
            return false;
        }

        return (tickData.tickCount % intervalUpdatePeriod) != (cascadeId - realtimeCount);
    };

    for (int32 cascadeId = 0; cascadeId < cacsadeInfos.size(); cascadeId++)
    {
        if (isCascadeCacheValid(cascadeId))
        {
            resultCtx.viewInfos[cascadeId] = cascadeHistory.viewInfos[cascadeId];
            resultCtx.depths[cascadeId]    = cascadeHistory.depths[cascadeId];
        }
        else
        {
            updateCascade(cascadeId);
        }
    }

    // Upload to GPU.
    auto viewBufferAndId = uploadBufferToGPU(cmd, "CascadeViewInstanceCullingInfo", resultCtx.viewInfos.data(), resultCtx.viewInfos.size());
    resultCtx.views = viewBufferAndId.first;
    resultCtx.viewsSRV = viewBufferAndId.second;

    for (int32 cascadeId = 0; cascadeId < config.cascadeConfig.cascadeCount; cascadeId++)
    {
        if (!isCascadeCacheValid(cascadeId))
        {
            chord::CountAndCmdBuffer countAndCmdBuffers = {};
            countAndCmdBuffers = instanceCulling(queue, renderCtx, viewBufferAndId.second, cascadeId, false);
            if (bCacheValid && sShadowHZBCullingEnable)
            {
                auto hzbCtx = buildHZB(queue, cascadeHistory.depths[cascadeId], true, false, false);

                constexpr bool bObjectUseLastFrameProject = false;
                detail::hzbCullingGeneric(
                    queue, 
                    hzbCtx, 
                    cascadeHistory.viewsSRV, 
                    cascadeId, 
                    bObjectUseLastFrameProject, 
                    renderCtx, 
                    countAndCmdBuffers.first, 
                    countAndCmdBuffers.second, 
                    countAndCmdBuffers);
            }

            static const auto depthStencilClear = VkClearDepthStencilValue{ 0.0f, 0 };
            queue.clearDepthStencil(resultCtx.depths[cascadeId], &depthStencilClear, VK_IMAGE_ASPECT_DEPTH_BIT);

            RenderTargets RTs{ };
            RTs.depthStencil = DepthStencilRT(
                resultCtx.depths[cascadeId],
                EDepthStencilOp::DepthWrite_StencilNop,
                ERenderTargetLoadStoreOp::Load_Store);
            renderMeshDepth(
                queue, 
                RTs, 
                renderCtx, 
                PASS_TYPE_DEPTH, 
                true,
                config.cascadeConfig.shadowBiasConst, 
                config.cascadeConfig.shadowBiasSlope,
                viewBufferAndId.second, 
                cascadeId, 
                countAndCmdBuffers.second, 
                countAndCmdBuffers.first);
        }
    }

    CascadeShadowDepthIds shadowDepthIds{};
    for (uint i = 0; i < resultCtx.depths.size(); i++)
    {
        shadowDepthIds.shadowDepth[i] = asSRV(queue, resultCtx.depths[i], helper::buildDepthImageSubresource());
    }

    resultCtx.cascadeShadowDepthIds = uploadBufferToGPU(cmd, "cascadeShadowDepthIds", &shadowDepthIds).second;

    if (renderCtx.timerLambda)
    {
        renderCtx.timerLambda("ShadowDepths", queue);
    }


    return resultCtx;
}

CascadeShadowHistory chord::extractCascadeShadowHistory(
    graphics::GraphicsQueue& queue, 
    const float3& direction,
    const CascadeShadowContext& shadowCtx, 
    const ApplicationTickData& tickData)
{
    CascadeShadowHistory result { }; 
    result.direction = direction;
    result.config    = shadowCtx.config;
    result.views     = shadowCtx.views;
    result.viewsSRV  = shadowCtx.viewsSRV;
    result.depths    = shadowCtx.depths;
    result.viewInfos = shadowCtx.viewInfos;
    // 

    return result;
}
