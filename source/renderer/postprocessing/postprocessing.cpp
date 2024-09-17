#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/hzb.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <shader/indirect_cmd.hlsl>

using namespace chord;
using namespace chord::graphics;

PRIVATE_GLOBAL_SHADER(IndirectCmdParamCS, "resource/shader/indirect_cmd.hlsl", "indirectCmdParamCS", EShaderStage::Compute);

PoolBufferGPUOnlyRef chord::indirectDispatchCmdFill(
    const std::string& name,
    graphics::GraphicsQueue& queue,
    uint groupSize,
    graphics::PoolBufferGPUOnlyRef countBuffer)
{
    auto cmdBuffer = getContext().getBufferPool().createGPUOnly(
        name, sizeof(math::uvec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

    {
        IndirectDispatchCmdPushConsts push{ };

        push.groupSize = groupSize;
        push.countBufferId = asSRV(queue, countBuffer);
        push.cmdBufferId = asUAV(queue, cmdBuffer);
        push.offset = 0;

        auto computeShader = getContext().getShaderLibrary().getShader<IndirectCmdParamCS>();
        addComputePass2(queue,
            name,
            getContext().computePipe(computeShader, name),
            push,
            math::uvec3(1, 1, 1));
    }

    return cmdBuffer;
}