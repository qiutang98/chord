#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/debug_line.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>

namespace chord
{
	using namespace graphics;

    static uint32 sDebugLineMaxVerticesCount = 1024 * 1024 / 4; // 1MB
    static AutoCVarRef cVarDebugLineMaxVerticesCount(
        "r.debugline.maxVerticesCount",
        sDebugLineMaxVerticesCount,
        "Debug line max vertices count, 0 meaning disable."
    );

    PRIVATE_GLOBAL_SHADER(DebugLineFillDrawCmdCS, "resource/shader/debug_line.hlsl", "gpuFillIndirectCS", EShaderStage::Compute);
    PRIVATE_GLOBAL_SHADER(DebugLineDrawPS, "resource/shader/debug_line.hlsl", "mainPS", EShaderStage::Pixel);

    class DebugLineDrawVS : public GlobalShader
    {
    public:
        DECLARE_SUPER_TYPE(GlobalShader);

        class DIM_GPU : SHADER_VARIANT_BOOL("DIM_GPU");
        using Permutation = TShaderVariantVector<DIM_GPU>;
    };
    IMPLEMENT_GLOBAL_SHADER(DebugLineDrawVS, "resource/shader/debug_line.hlsl", "mainVS", EShaderStage::Vertex);

    DebugLineCtx allocateDebugLineCtx()
    {
        DebugLineCtx ctx { };

        ctx.gpuMaxCount = math::clamp(sDebugLineMaxVerticesCount, 0U, 67108864U); // MAX 256 MB.
        ctx.gpuVertices = getContext().getBufferPool().createGPUOnly(
            "GPUDebugLineVerticesBuffer",
            sizeof(LineDrawVertex) * ctx.gpuMaxCount,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        ctx.gpuCountBuffer = getContext().getBufferPool().createGPUOnly(
            "GPUDebugLineCountBuffer",
            sizeof(uint) * 2, // UINT 2: #0 is count, #1 use for print fallback message.
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

        return ctx;
    }

    void DebugLineCtx::prepareForRender(graphics::GraphicsQueue& queue)
    {
        asUAV(queue, gpuVertices);
        asUAV(queue, gpuCountBuffer);

        queue.clearUAV(gpuCountBuffer);
    }

    void chord::debugLine(GraphicsQueue& queue, const DebugLineCtx& ctx, PoolTextureRef depthImage, PoolTextureRef outImage)
    {
        RenderTargets RTs{ };
        {
            RTs.RTs[0] = RenderTargetRT(outImage, ERenderTargetLoadStoreOp::Load_Store);
            RTs.bEnableBlends[0] = VK_TRUE;
            RTs.colorBlendEquations[0] = getBlendMode(EBlendMode::Translucency);
            RTs.depthStencil = DepthStencilRT(
                depthImage,
                EDepthStencilOp::DepthRead_StnecilRead,
                ERenderTargetLoadStoreOp::Load_Nope);
        }
        auto pixelShader = getContext().getShaderLibrary().getShader<DebugLineDrawPS>();

        // Render cpu require debug line.
        if (!ctx.vertices.empty())
        {
            auto verticesBuffer = getContext().getBufferPool().createHostVisible(
                "CPUDebugLineVerticesBuffer",
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                SizedBuffer(sizeof(LineDrawVertex) * ctx.vertices.size(), (void*)ctx.vertices.data()));

            DebugLinePushConst pushConst { };
            pushConst.cameraViewId = ctx.cameraViewBufferId;
            pushConst.debugLineCPUVertices = asSRV(queue, verticesBuffer);

            DebugLineDrawVS::Permutation vertexShaderPermutation;
            vertexShaderPermutation.set<DebugLineDrawVS::DIM_GPU>(false);
            auto vertexShader = getContext().getShaderLibrary().getShader<DebugLineDrawVS>(vertexShaderPermutation);

            auto pipeline = getContext().graphicsPipe(
                vertexShader, pixelShader,
                "DebugLine-CPU",
                std::move(RTs.getRTsFormats()),
                RTs.getDepthStencilFormat(),
                RTs.getDepthStencilFormat());

            addDrawPass(
                queue,
                "GLTF Basepass: Raster",
                pipeline,
                RTs,
                [&](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
                {
                    pipe->pushConst(cmd, pushConst);

                    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
                    vkCmdSetDepthTestEnable(cmd, VK_TRUE);
                    vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
                    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_GREATER_OR_EQUAL);
                    vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_LINE);

                    vkCmdDraw(cmd, ctx.vertices.size(), 1, 0, 0);
                });
        }

        // Render gpu require debug line.
        if (ctx.gpuMaxCount > 0)
        {
            auto drawCmdBuffer = getContext().getBufferPool().createGPUOnly(
                "GPUDebugLineDrawCmdBuffer",
                sizeof(uint4),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

            asSRV(queue, ctx.gpuCountBuffer);
            {
                DebugLinePushConst pushConst{ };
                pushConst.cameraViewId = ctx.cameraViewBufferId;
                pushConst.gpuDrawCmdId = asUAV(queue, drawCmdBuffer);

                auto computeShader = getContext().getShaderLibrary().getShader<DebugLineFillDrawCmdCS>();

                addComputePass2(queue, "FillGPUDebugLineCmd",
                    getContext().computePipe(computeShader, "FillGPUDebugLineCmdPipe"), pushConst, { 1, 1, 1 });
            }

            asSRV(queue, ctx.gpuVertices);
            queue.transition(drawCmdBuffer, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
            {
                DebugLinePushConst pushConst{ };
                pushConst.cameraViewId = ctx.cameraViewBufferId;

                DebugLineDrawVS::Permutation vertexShaderPermutation;
                vertexShaderPermutation.set<DebugLineDrawVS::DIM_GPU>(true);
                auto vertexShader = getContext().getShaderLibrary().getShader<DebugLineDrawVS>(vertexShaderPermutation);

                auto pipeline = getContext().graphicsPipe(
                    vertexShader, pixelShader,
                    "DebugLine-GPU",
                    std::move(RTs.getRTsFormats()),
                    RTs.getDepthStencilFormat(),
                    RTs.getDepthStencilFormat(),
                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

                addDrawPass(
                    queue,
                    "GLTF Basepass: Raster",
                    pipeline,
                    RTs,
                    [&](graphics::GraphicsQueue& queue, graphics::GraphicsPipelineRef pipe, VkCommandBuffer cmd)
                    {
                        pipe->pushConst(cmd, pushConst);

                        vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
                        vkCmdSetDepthTestEnable(cmd, VK_TRUE);
                        vkCmdSetDepthWriteEnable(cmd, VK_FALSE);
                        vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_GREATER_OR_EQUAL);
                        vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_LINE);

                        vkCmdDrawIndirect(cmd, drawCmdBuffer->get(), 0, 1, sizeof(uint4));
                    });
            }

        }
    }
}