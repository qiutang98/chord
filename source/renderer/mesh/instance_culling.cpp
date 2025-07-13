#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader_compiler/shader.h>
#include <shader/instance_culling.hlsl>
#include <shader/pipeline_filter.hlsl>
#include <shader/hzb_mainview_culling.hlsl>
#include <shader/hzb_culling_generic.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/postprocessing/postprocessing.h>
#include <renderer/renderer.h>

namespace chord
{

    static uint32 sInstanceCullingEnableFrustumCulling = 1;
    static AutoCVarRef cVarInstanceCullingEnableFrustumCulling(
        "r.instanceculling.frustumCulling",
        sInstanceCullingEnableFrustumCulling,
        "Enable frustum culling or not."
    );

    static uint32 sInstanceCullingEnableMeshletConeCulling = 1;
    static AutoCVarRef cVarInstanceCullingEnableMeshletConeCulling(
        "r.instanceculling.meshletConeCulling",
        sInstanceCullingEnableMeshletConeCulling,
        "Enable meshlet cone culling or not."
    );

    static uint32 sInstanceCullingEnableHZBCulling = 1;
    static AutoCVarRef cVarInstanceCullingEnableHZBCulling(
        "r.instanceculling.hzbCulling",
        sInstanceCullingEnableHZBCulling,
        "Enable meshlet hzb culling or not."
    );

    static uint32 sInstanceCullingShaderDebugMode = 0;
    static AutoCVarRef cVarInstanceCullingShaderDebugMode(
        "r.instanceculling.shaderDebugMode",
        sInstanceCullingShaderDebugMode,
        "**** Instance culling shader debug mode ****"
        "  0. default state, do nothing."
        "  1. draw visible meshlet bounds box."
        "  2. draw stage#1 visible meshlet bounds box."
    );

    bool chord::enableGLTFHZBCulling()
    {
        return sInstanceCullingEnableHZBCulling != 0;
    }
}

namespace chord::graphics
{
    static inline uint32 getInstanceCullingSwitchFlags()
    {
        uint32 result = 0;
        if (sInstanceCullingEnableFrustumCulling != 0) { result = shaderSetFlag(result, kFrustumCullingEnableBit); }
        if (enableGLTFHZBCulling()) { result = shaderSetFlag(result, kHZBCullingEnableBit); }
        if (sInstanceCullingEnableMeshletConeCulling != 0) { result = shaderSetFlag(result, kMeshletConeCullEnableBit); }
        return result;
    }

    static inline bool shouldPrintDebugBox(bool bFirstStage)
    {
        if (sInstanceCullingShaderDebugMode == 1) { return true; }
        if (sInstanceCullingShaderDebugMode == 2) { return !bFirstStage; }
        return false;
    }

    PRIVATE_GLOBAL_SHADER(InstanceCullingCS, "resource/shader/instance_culling.hlsl", "instanceCullingCS", EShaderStage::Compute);
    PRIVATE_GLOBAL_SHADER(ClusterGroupCullingCS, "resource/shader/instance_culling.hlsl", "clusterGroupCullingCS", EShaderStage::Compute);
}

namespace chord
{
    CountAndCmdBuffer chord::instanceCulling(
        graphics::GraphicsQueue& queue,
        const GLTFRenderContext& ctx,
        uint instanceCullingViewInfo,
        uint instanceCullingViewInfoOffset,
        bool bPrintTimer)
    {
        using namespace graphics;

        if (!shouldRenderGLTF(ctx))
        {
            return { nullptr, nullptr };
        }

        // Upload to GPU view id.
        auto& pool = getContext().getBufferPool();

        uint32 cameraView = ctx.cameraView;
        const uint objectCount = ctx.gltfObjectCount;
        const uint lod0MeshletCount = ctx.perframeCollect->gltfLod0MeshletCount;
        const uint clusterGroupCount = ctx.perframeCollect->gltfMeshletGroupCount;

        check(objectCount == ctx.perframeCollect->gltfPrimitives.size());
        check(lod0MeshletCount > 0 && clusterGroupCount > 0);
        queue.checkRecording();

        InstanceCullingPushConst pushTemplate{ .cameraViewId = cameraView, .switchFlags = getInstanceCullingSwitchFlags() };
        pushTemplate.instanceViewId = instanceCullingViewInfo;
        pushTemplate.instanceViewOffset = instanceCullingViewInfoOffset;

        auto clusterGroupCountBuffer = pool.createGPUOnly("clusterGroupCountBuffer", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto clusterGroupIdBuffer = pool.createGPUOnly("clusterGroupIdBuffer", sizeof(uint2) * clusterGroupCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        queue.clearUAV(clusterGroupCountBuffer);
        {
            ScopePerframeMarker marker(queue, "InstanceCulling: PerObject");

            InstanceCullingPushConst push = pushTemplate;
            push.clusterGroupCountBuffer = asUAV(queue, clusterGroupCountBuffer);
            push.clusterGroupIdBuffer = asUAV(queue, clusterGroupIdBuffer);

            auto computeShader = getContext().getShaderLibrary().getShader<InstanceCullingCS>();

            addComputePass2(queue,
                "InstanceCulling: PerObject",
                getContext().computePipe(computeShader, "InstanceCullingPipe: PerObject"),
                push,
                math::uvec3(objectCount, 1, 1));
        }

        if (ctx.timerLambda && bPrintTimer)
        {
            ctx.timerLambda("GLTF Object Culling", queue);
        }

        // Indirect dispatch parameter.
        auto clusterGroupCullCmdBuffer = indirectDispatchCmdFill("ClusterGroupCullParam", queue, 64, clusterGroupCountBuffer);

        auto drawMeshletCountBuffer = pool.createGPUOnly("drawMeshletCountBuffer", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto drawMeshletCmdBuffer = pool.createGPUOnly("drawMeshletCmdBuffer", sizeof(uint3) * lod0MeshletCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        queue.clearUAV(drawMeshletCountBuffer);
        {
            ScopePerframeMarker marker(queue, "InstanceCulling: BVHTraverse");

            InstanceCullingPushConst push = pushTemplate;
            push.clusterGroupCountBuffer = asSRV(queue, clusterGroupCountBuffer);
            push.clusterGroupIdBuffer = asSRV(queue, clusterGroupIdBuffer);
            push.drawedMeshletCountId = asUAV(queue, drawMeshletCountBuffer);
            push.drawedMeshletCmdId = asUAV(queue, drawMeshletCmdBuffer);

            auto computeShader = getContext().getShaderLibrary().getShader<ClusterGroupCullingCS>();
            addIndirectComputePass2(queue,
                "InstanceCulling: ClusterGroupCulling",
                getContext().computePipe(computeShader, "InstanceCullingPipe: ClusterGroupCulling"),
                push,
                clusterGroupCullCmdBuffer);
        }

        return { drawMeshletCountBuffer, drawMeshletCmdBuffer };
    }
}

namespace chord::graphics
{
    // 
    PRIVATE_GLOBAL_SHADER(FilterPipelineParamCS, "resource/shader/pipeline_filter.hlsl", "filterPipelineParamCS", EShaderStage::Compute);
    PRIVATE_GLOBAL_SHADER(FillPipelineDrawParamCS, "resource/shader/pipeline_filter.hlsl", "fillPipelineDrawParamCS", EShaderStage::Compute);

    class HZBMainViewCullingCS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);

        class SV_bFirstStage : SHADER_VARIANT_BOOL("DIM_HZB_CULLING_PHASE_0");
        class SV_bPrintDebugBox : SHADER_VARIANT_BOOL("DIM_PRINT_DEBUG_BOX");
        using Permutation = TShaderVariantVector<SV_bFirstStage, SV_bPrintDebugBox>;
    };
    IMPLEMENT_GLOBAL_SHADER(HZBMainViewCullingCS, "resource/shader/hzb_mainview_culling.hlsl", "hzbMainViewCullingCS", EShaderStage::Compute);

    // Generic culling for hzb.
    PRIVATE_GLOBAL_SHADER(HZBGenericCullingCS, "resource/shader/hzb_culling_generic.hlsl", "mainCS", EShaderStage::Compute);
}

namespace chord
{
    graphics::PoolBufferGPUOnlyRef chord::detail::filterPipelineIndirectDispatchCmd(
        graphics::GraphicsQueue& queue,
        const GLTFRenderContext& renderCtx, 
        const std::string& name, 
        graphics::PoolBufferGPUOnlyRef countBuffer)
    {
        using namespace graphics;

        auto meshletCullCmdBuffer = getContext().getBufferPool().createGPUOnly(name, sizeof(math::uvec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        {
            PipelineFilterPushConst push{ };
            push.drawedMeshletCountId = asSRV(queue, countBuffer);
            push.drawedMeshletCmdId = asUAV(queue, meshletCullCmdBuffer);

            auto computeShader = getContext().getShaderLibrary().getShader<FilterPipelineParamCS>();
            addComputePass2(queue,
                name,
                getContext().computePipe(computeShader, name),
                push,
                math::uvec3(1, 1, 1));
        }

        return meshletCullCmdBuffer;
    }

    template<typename T>
    static void fillHZBPushParam(graphics::GraphicsQueue& queue, const GLTFRenderContext& renderCtx, T& push, const HZBContext& inHzb)
    {
        static_assert(std::is_same_v<T, HZBCullingPushConst> || std::is_same_v<T, HZBCullingGenericPushConst>);

        // Texture SRV.
        push.hzb = asSRV(queue, inHzb.minHZB); //

        // Mip count.
        // push.hzbMipCount = inHzb.mipmapLevelCount;

        // UV transformer.
        // push.uv2HzbX = float(inHzb.mip0ValidArea.x) / float(inHzb.dimension.x);
        // push.uv2HzbY = float(inHzb.mip0ValidArea.y) / float(inHzb.dimension.y);

        // Dimension
        // push.hzbMip0Width = inHzb.dimension.x;
        // push.hzbMip0Height = inHzb.dimension.y;
    };

    void chord::detail::hzbCullingGeneric(
        graphics::GraphicsQueue& queue, 
        const HZBContext& inHzb, 
        float extentScale,
        uint instanceViewId,
        uint instanceViewOffset,
        bool bObjectUseLastFrameProject,
        const GLTFRenderContext& renderCtx,
        graphics::PoolBufferGPUOnlyRef inCountBuffer, 
        graphics::PoolBufferGPUOnlyRef inCmdBuffer, 
        CountAndCmdBuffer& outBuffer)
    {
        ZoneScoped;
        using namespace graphics;

        const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;
        auto& pool = getContext().getBufferPool();

        PoolBufferGPUOnlyRef countBufferStage1 = nullptr;
        PoolBufferGPUOnlyRef cmdBufferStage1 = nullptr;

        // Indirect dispatch parameter.
        auto meshletCullCmdBuffer = indirectDispatchCmdFill("meshletCullCmdBufferParam", queue, 64, inCountBuffer);

        // Count and cmd.
        auto countBuffer = pool.createGPUOnly("CountBuffer", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto drawMeshletCmdBuffer = pool.createGPUOnly("CmdBuffer", sizeof(uint3) * lod0MeshletCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        // Clear count buffer.
        queue.clearUAV(countBuffer);

        HZBCullingGenericPushConst pushConst{ .cameraViewId = renderCtx.cameraView, .switchFlags = getInstanceCullingSwitchFlags() };
        fillHZBPushParam(queue, renderCtx, pushConst, inHzb);

        pushConst.extentScale                = extentScale;
        pushConst.instanceViewId             = instanceViewId;
        pushConst.instanceViewOffset         = instanceViewOffset;
        pushConst.drawedMeshletCountId       = asSRV(queue, inCountBuffer);
        pushConst.drawedMeshletCmdId         = asSRV(queue, inCmdBuffer);
        pushConst.drawedMeshletCountId_1     = asUAV(queue, countBuffer);
        pushConst.drawedMeshletCmdId_1       = asUAV(queue, drawMeshletCmdBuffer);
        pushConst.bObjectUseLastFrameProject = bObjectUseLastFrameProject;

        auto computeShader = getContext().getShaderLibrary().getShader<HZBGenericCullingCS>();
        addIndirectComputePass2(queue,
            "HZB Generic Culling",
            getContext().computePipe(computeShader, "HZBCullingPipe"),
            pushConst,
            meshletCullCmdBuffer);

        outBuffer.first = countBuffer;
        outBuffer.second = drawMeshletCmdBuffer;
    }

    chord::CountAndCmdBuffer chord::detail::hzbCulling(
        graphics::GraphicsQueue& queue,
        const HZBContext& inHzb,
        const GLTFRenderContext& renderCtx, 
        bool bFirstStage,
        graphics::PoolBufferGPUOnlyRef inCountBuffer,
        graphics::PoolBufferGPUOnlyRef inCmdBuffer,
        CountAndCmdBuffer& outBuffer)
    {
        using namespace graphics;

        const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;
        auto& pool = getContext().getBufferPool();

        PoolBufferGPUOnlyRef countBufferStage1 = nullptr;
        PoolBufferGPUOnlyRef cmdBufferStage1 = nullptr;

        // Indirect dispatch parameter.
        auto meshletCullCmdBuffer = indirectDispatchCmdFill("meshletCullCmdBufferParam", queue, 64, inCountBuffer);

        // Count and cmd.
        auto countBuffer = pool.createGPUOnly("CountBuffer", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto drawMeshletCmdBuffer = pool.createGPUOnly("CmdBuffer", sizeof(uint3) * lod0MeshletCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    
        // Clear count buffer.
        queue.clearUAV(countBuffer);

        if (bFirstStage)
        {
            countBufferStage1 = pool.createGPUOnly("CountBufferStage", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            cmdBufferStage1 = pool.createGPUOnly("CmdBufferStage", sizeof(uint3) * lod0MeshletCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
        
            // Clear count buffer.
            queue.clearUAV(countBufferStage1);
        }

        HZBCullingPushConst pushConst { .cameraViewId = renderCtx.cameraView, .switchFlags = getInstanceCullingSwitchFlags() };
        fillHZBPushParam(queue, renderCtx, pushConst, inHzb);

        pushConst.drawedMeshletCountId   = asSRV(queue, inCountBuffer);
        pushConst.drawedMeshletCmdId     = asSRV(queue, inCmdBuffer);
        pushConst.drawedMeshletCountId_1 = asUAV(queue, countBuffer);
        pushConst.drawedMeshletCmdId_1   = asUAV(queue, drawMeshletCmdBuffer);

        if (bFirstStage)
        {
            pushConst.drawedMeshletCountId_2 = asUAV(queue, countBufferStage1);
            pushConst.drawedMeshletCmdId_2   = asUAV(queue, cmdBufferStage1);
        }

        HZBMainViewCullingCS::Permutation permutation;
        permutation.set<HZBMainViewCullingCS::SV_bFirstStage>(bFirstStage);
        permutation.set<HZBMainViewCullingCS::SV_bPrintDebugBox>(shouldPrintDebugBox(bFirstStage));

        auto computeShader = getContext().getShaderLibrary().getShader<HZBMainViewCullingCS>(permutation);
        addIndirectComputePass2(queue,
            bFirstStage ? "HZBCulling: Stage#0" : "HZBCulling: Stage#1",
            getContext().computePipe(computeShader, "HZBCullingPipe"),
            pushConst,
            meshletCullCmdBuffer);

        outBuffer.first = countBuffer;
        outBuffer.second = drawMeshletCmdBuffer;

        return { countBufferStage1, cmdBufferStage1 };
    }

    chord::CountAndCmdBuffer chord::detail::filterPipeForVisibility(
        graphics::GraphicsQueue& queue,
        const GLTFRenderContext& renderCtx, 
        graphics::PoolBufferGPUOnlyRef dispatchCmd,
        graphics::PoolBufferGPUOnlyRef inCmdBuffer,
        graphics::PoolBufferGPUOnlyRef inCountBuffer,
        uint alphaMode, 
        uint bTwoSide)
    {
        using namespace graphics;

        const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;
        auto& pool = getContext().getBufferPool();

        auto countBuffer = pool.createGPUOnly("CountPostFilter", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto cmdBuffer   = pool.createGPUOnly("CmdBufferStage", sizeof(uint3) * lod0MeshletCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        queue.clearUAV(countBuffer);

        PipelineFilterPushConst pushConst{ .cameraViewId = renderCtx.cameraView };

        pushConst.drawedMeshletCountId   = asSRV(queue, inCountBuffer);
        pushConst.drawedMeshletCmdId     = asSRV(queue, inCmdBuffer);
        pushConst.drawedMeshletCountId_1 = asUAV(queue, countBuffer);
        pushConst.drawedMeshletCmdId_1   = asUAV(queue, cmdBuffer);
        pushConst.targetTwoSide          = bTwoSide;
        pushConst.targetAlphaMode        = alphaMode;

        addIndirectComputePass2(queue, "PipelineFilter", getContext().computePipe(getContext().getShaderLibrary().getShader<FillPipelineDrawParamCS>(), "PipelineFilter"), pushConst, dispatchCmd);

        return { countBuffer, cmdBuffer };
    }
}
