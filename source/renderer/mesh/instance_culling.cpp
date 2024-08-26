#pragma once

#include <graphics/resource.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <renderer/fullscreen.h>
#include <renderer/mesh/gltf_rendering.h>
#include <shader/shader.h>
#include <shader/instance_culling.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <renderer/postprocessing/postprocessing.h>

using namespace chord;
using namespace chord::graphics;

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
);

bool chord::enableGLTFHZBCulling()
{
    return sInstanceCullingEnableHZBCulling != 0;
}

static inline uint32 getInstanceCullingSwitchFlags()
{
    uint32 result = 0;
    if (sInstanceCullingEnableFrustumCulling != 0)     { result = shaderSetFlag(result, kFrustumCullingEnableBit); }
    if (enableGLTFHZBCulling())                        { result = shaderSetFlag(result, kHZBCullingEnableBit); }
    if (sInstanceCullingEnableMeshletConeCulling != 0) { result = shaderSetFlag(result, kMeshletConeCullEnableBit); }
    return result;
}

static inline bool shouldPrintDebugBox()
{
    return sInstanceCullingShaderDebugMode == 1;
}

PRIVATE_GLOBAL_SHADER(InstanceCullingCS, "resource/shader/instance_culling.hlsl", "instanceCullingCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(ClusterGroupCullingCS, "resource/shader/instance_culling.hlsl", "clusterGroupCullingCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(FillCullingParamCS, "resource/shader/instance_culling.hlsl", "fillCullingParamCS", EShaderStage::Compute);

class HZBCullCS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);

    class SV_bFirstStage : SHADER_VARIANT_BOOL("DIM_HZB_CULLING_PHASE_0");
    class SV_bPrintDebugBox : SHADER_VARIANT_BOOL("DIM_PRINT_DEBUG_BOX");
    using Permutation = TShaderVariantVector<SV_bFirstStage, SV_bPrintDebugBox>;
};
IMPLEMENT_GLOBAL_SHADER(HZBCullCS, "resource/shader/instance_culling.hlsl", "HZBCullingCS", EShaderStage::Compute);

class FillPipelineDrawParamCS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);

    class SV_bMeshShader : SHADER_VARIANT_BOOL("DIM_MESH_SHADER");
    using Permutation = TShaderVariantVector<SV_bMeshShader>;
};
IMPLEMENT_GLOBAL_SHADER(FillPipelineDrawParamCS, "resource/shader/instance_culling.hlsl", "fillPipelineDrawParamCS", EShaderStage::Compute);

PoolBufferGPUOnlyRef chord::detail::fillIndirectDispatchCmd(GLTFRenderContext& renderCtx, const std::string& name, PoolBufferGPUOnlyRef countBuffer)
{
    auto meshletCullCmdBuffer = getContext().getBufferPool().createGPUOnly(name, sizeof(uvec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    {
        InstanceCullingPushConst push{ };
        push.drawedMeshletCountId = asSRV(renderCtx.queue, countBuffer);
        push.meshletCullCmdId = asUAV(renderCtx.queue, meshletCullCmdBuffer);

        auto computeShader = getContext().getShaderLibrary().getShader<FillCullingParamCS>();
        addComputePass2(renderCtx.queue,
            name,
            getContext().computePipe(computeShader, name),
            push,
            math::uvec3(1, 1, 1));
    }

    return meshletCullCmdBuffer;
};


void chord::instanceCulling(GLTFRenderContext& ctx)
{
    if (!shouldRenderGLTF(ctx))
    {
        return;
    }

    auto& queue = ctx.queue;
    auto& gbuffers = ctx.gbuffers;
    auto& pool = getContext().getBufferPool();

    uint32 cameraView = ctx.cameraView;
    const uint objectCount = ctx.gltfObjectCount;
    const uint lod0MeshletCount = ctx.perframeCollect->gltfLod0MeshletCount;
    const uint clusterGroupCount = ctx.perframeCollect->gltfMeshletGroupCount;

    check(objectCount == ctx.perframeCollect->gltfPrimitives.size());
    check(lod0MeshletCount > 0 && clusterGroupCount > 0);
    queue.checkRecording();

    const InstanceCullingPushConst pushTemplate { .cameraViewId = cameraView, .switchFlags = getInstanceCullingSwitchFlags() };

    auto clusterGroupCountBuffer = pool.createGPUOnly("clusterGroupCountBuffer", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    auto clusterGroupIdBuffer = pool.createGPUOnly("clusterGroupIdBuffer", sizeof(uint2) * clusterGroupCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    queue.clearUAV(clusterGroupCountBuffer);
    {
        ScopePerframeMarker marker(queue, "InstanceCulling: PerObject");

        InstanceCullingPushConst push = pushTemplate;
        push.clusterGroupCountBuffer = asUAV(queue, clusterGroupCountBuffer);
        push.clusterGroupIdBuffer    = asUAV(queue, clusterGroupIdBuffer);

        auto computeShader = getContext().getShaderLibrary().getShader<InstanceCullingCS>();

        addComputePass2(queue,
            "InstanceCulling: PerObject",
            getContext().computePipe(computeShader, "InstanceCullingPipe: PerObject"),
            push,
            math::uvec3(objectCount, 1, 1));
    }

    if (ctx.timerLambda)
    {
        ctx.timerLambda("GLTF Object Culling", queue);
    }


    // Indirect dispatch parameter.
    auto clusterGroupCullCmdBuffer = chord::detail::fillIndirectDispatchCmd(ctx, "ClusterGroupCullParam", clusterGroupCountBuffer);

    auto drawMeshletCountBuffer = pool.createGPUOnly("drawMeshletCountBuffer", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    auto drawMeshletCmdBuffer   = pool.createGPUOnly("drawMeshletCmdBuffer",   sizeof(uint3) * lod0MeshletCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    queue.clearUAV(drawMeshletCountBuffer);
    {
        ScopePerframeMarker marker(queue, "InstanceCulling: BVHTraverse");

        InstanceCullingPushConst push = pushTemplate;
        push.clusterGroupCountBuffer  = asSRV(queue, clusterGroupCountBuffer);
        push.clusterGroupIdBuffer     = asSRV(queue, clusterGroupIdBuffer);
        push.drawedMeshletCountId     = asUAV(queue, drawMeshletCountBuffer);
        push.drawedMeshletCmdId       = asUAV(queue, drawMeshletCmdBuffer);

        auto computeShader = getContext().getShaderLibrary().getShader<ClusterGroupCullingCS>();
        addIndirectComputePass2(queue,
            "InstanceCulling: ClusterGroupCulling",
            getContext().computePipe(computeShader, "InstanceCullingPipe: ClusterGroupCulling"),
            push,
            clusterGroupCullCmdBuffer);
    }

    ctx.postBasicCullingCtx.meshletCmdBuffer   = drawMeshletCmdBuffer;
    ctx.postBasicCullingCtx.meshletCountBuffer = drawMeshletCountBuffer;
}

static void fillHZBPushParam(GLTFRenderContext& renderCtx, InstanceCullingPushConst& push, const HZBContext& inHzb)
{
    // Texture SRV.
    push.hzb = asSRV(renderCtx.queue, inHzb.minHZB); //

    // Mip count.
    push.hzbMipCount = inHzb.mipmapLevelCount;

    // UV transformer.
    push.uv2HzbX = float(inHzb.mip0ValidArea.x) / float(inHzb.dimension.x);
    push.uv2HzbY = float(inHzb.mip0ValidArea.y) / float(inHzb.dimension.y);

    // Dimension
    push.hzbMip0Width = inHzb.dimension.x;
    push.hzbMip0Height = inHzb.dimension.y;
};


chord::detail::CountAndCmdBuffer chord::detail::hzbCulling(
    GLTFRenderContext& renderCtx, bool bFirstStage, PoolBufferGPUOnlyRef inCountBuffer, PoolBufferGPUOnlyRef inCmdBuffer, CountAndCmdBuffer& outBuffer)
{
    const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;

    auto& queue = renderCtx.queue;
    auto& pool = getContext().getBufferPool();

    PoolBufferGPUOnlyRef countBufferStage1 = nullptr;
    PoolBufferGPUOnlyRef cmdBufferStage1 = nullptr;

    // Indirect dispatch parameter.
    auto meshletCullCmdBuffer = chord::detail::fillIndirectDispatchCmd(renderCtx, "meshletCullCmdBufferParam", inCountBuffer);

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

    InstanceCullingPushConst pushConst { .cameraViewId = renderCtx.cameraView, .switchFlags = getInstanceCullingSwitchFlags() };
    fillHZBPushParam(renderCtx, pushConst, renderCtx.history.hzbCtx);

    pushConst.drawedMeshletCountId   = asSRV(queue, inCountBuffer);
    pushConst.drawedMeshletCmdId     = asSRV(queue, inCmdBuffer);
    pushConst.drawedMeshletCountId_1 = asUAV(queue, countBuffer);
    pushConst.drawedMeshletCmdId_1   = asUAV(queue, drawMeshletCmdBuffer);

    if (bFirstStage)
    {
        pushConst.drawedMeshletCountId_2 = asUAV(queue, countBufferStage1);
        pushConst.drawedMeshletCmdId_2   = asUAV(queue, cmdBufferStage1);
    }

    HZBCullCS::Permutation permutation;
    permutation.set<HZBCullCS::SV_bFirstStage>(bFirstStage);
    permutation.set<HZBCullCS::SV_bPrintDebugBox>(shouldPrintDebugBox());

    auto computeShader = getContext().getShaderLibrary().getShader<HZBCullCS>(permutation);
    addIndirectComputePass2(queue,
        bFirstStage ? "HZBCulling: Stage#0" : "HZBCulling: Stage#1",
        getContext().computePipe(computeShader, "HZBCullingPipe"),
        pushConst,
        meshletCullCmdBuffer);

    outBuffer.first = countBuffer;
    outBuffer.second = drawMeshletCmdBuffer;

    return { countBufferStage1, cmdBufferStage1 };
}

chord::detail::CountAndCmdBuffer chord::detail::filterPipeForVisibility(bool bMeshShader, GLTFRenderContext& renderCtx, PoolBufferGPUOnlyRef dispatchCmd, PoolBufferGPUOnlyRef inCmdBuffer, PoolBufferGPUOnlyRef inCountBuffer, uint alphaMode, uint bTwoSide)
{
    const uint lod0MeshletCount = renderCtx.perframeCollect->gltfLod0MeshletCount;
    auto& queue = renderCtx.queue;
    auto& pool = getContext().getBufferPool();

    // Cmd stripe for different shader mode.
    const uint32 cmdStripeSize = bMeshShader ? sizeof(uint3) : sizeof(GLTFMeshletDrawCmd);

    auto countBuffer = pool.createGPUOnly("CountPostFilter", sizeof(uint), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    auto cmdBuffer   = pool.createGPUOnly("CmdBufferStage", cmdStripeSize * lod0MeshletCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

    queue.clearUAV(countBuffer);

    InstanceCullingPushConst pushConst{ .cameraViewId = renderCtx.cameraView, .switchFlags = getInstanceCullingSwitchFlags() };

    pushConst.drawedMeshletCountId   = asSRV(queue, inCountBuffer);
    pushConst.drawedMeshletCmdId     = asSRV(queue, inCmdBuffer);
    pushConst.drawedMeshletCountId_1 = asUAV(queue, countBuffer);
    pushConst.drawedMeshletCmdId_1   = asUAV(queue, cmdBuffer);
    pushConst.targetTwoSide          = bTwoSide;
    pushConst.targetAlphaMode        = alphaMode;

    FillPipelineDrawParamCS::Permutation permutation;
    permutation.set<FillPipelineDrawParamCS::SV_bMeshShader>(bMeshShader);

    addIndirectComputePass2(queue, "PipelineFilter", getContext().computePipe(getContext().getShaderLibrary().getShader<FillPipelineDrawParamCS>(permutation), "PipelineFilter"), pushConst, dispatchCmd);

    return { countBuffer, cmdBuffer };
}