#include <shader/shader.h>
#include <shader/compiler.h>
#include <renderer/postprocessing/postprocessing.h>
#include <shader/hzb.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/base.h>
#include <shader/indirect_cmd.hlsl>
#include <shader/auto_exposure.hlsl>
#include <shader/histogram.hlsl>
#include <shader/apply_exposure.hlsl>
#include <shader/debug_blit.hlsl>

using namespace chord;
using namespace chord::graphics;

PRIVATE_GLOBAL_SHADER(HistogramCS, "resource/shader/histogram.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(AutoExposureCS, "resource/shader/auto_exposure.hlsl", "mainCS", EShaderStage::Compute);
PRIVATE_GLOBAL_SHADER(ApplyExposureCS, "resource/shader/apply_exposure.hlsl", "mainCS", EShaderStage::Compute);

PRIVATE_GLOBAL_SHADER(IndirectCmdParamCS, "resource/shader/indirect_cmd.hlsl", "indirectCmdParamCS", EShaderStage::Compute);

PRIVATE_GLOBAL_SHADER(DebugBlitCS, "resource/shader/debug_blit.hlsl", "mainCS", EShaderStage::Compute);

void chord::debugBlitColor(
    graphics::GraphicsQueue& queue, 
    graphics::PoolTextureRef input, 
    graphics::PoolTextureRef output)
{
    DebugBlitPushConsts pushConst{};
    pushConst.dimension = { output->get().getExtent().width, output->get().getExtent().height };
    pushConst.pointClampSamplerId = getContext().getSamplerManager().pointClampEdge().index.get();
    pushConst.SRV = asSRV(queue, input);
    pushConst.UAV = asUAV(queue, output);

    const uint2 dispatchDim = divideRoundingUp(pushConst.dimension, uint2(8));
    auto computeShader = getContext().getShaderLibrary().getShader<DebugBlitCS>();
    addComputePass2(queue,
        "DebugBlitCS",
        getContext().computePipe(computeShader, "DebugBlitCS"),
        pushConst,
        { dispatchDim.x, dispatchDim.y, 1 });
}

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

PoolBufferGPUOnlyRef chord::computeAutoExposure(
    graphics::GraphicsQueue& queue, 
    graphics::PoolTextureRef color,
    const PostprocessConfig& config,
    graphics::PoolBufferGPUOnlyRef historyExposure,
    float deltaTime)
{
    constexpr float kExposureMaxEv = 9.0f;
    constexpr float kExposureMinEv = -9.0f;
    constexpr float kExposureDiff = kExposureMaxEv - kExposureMinEv;
    constexpr float kExposureScale = 1.0f / kExposureDiff;
    constexpr float kExposureOffset = -kExposureMinEv * kExposureScale;

    auto& bufferPool = getContext().getBufferPool();

    auto histogramBuffer = bufferPool.createGPUOnly("histogramBuffer",
        sizeof(uint) * kHistogramBinCount,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    queue.clearUAV(histogramBuffer, 0U);

    {
        HistogramPassPushConsts pushConsts{};
        pushConsts.UAV = asUAV(queue, histogramBuffer);
        pushConsts.SRV = asSRV(queue, color);

        pushConsts.a = kExposureScale;
        pushConsts.b = kExposureOffset;

        uint2 dim = { color->get().getExtent().width, color->get().getExtent().height };

        const uint2 dispatchDim = divideRoundingUp(dim, uint2(32));

        auto computeShader = getContext().getShaderLibrary().getShader<HistogramCS>();
        addComputePass2(queue,
            "Histogram",
            getContext().computePipe(computeShader, "Histogram"),
            pushConsts,
            { dispatchDim.x, dispatchDim.y, 1 });
    }

    auto exposureBuffer = bufferPool.createGPUOnly("exposureBuffer",
        sizeof(uint),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    {
        AutoExposurePushConsts pushConsts{};
        pushConsts.SRV_histogram = asSRV(queue, histogramBuffer);
        pushConsts.SRV_historyExposure = historyExposure == nullptr ? kUnvalidIdUint32 : asSRV(queue, historyExposure);
        pushConsts.UAV_exposure = asUAV(queue, exposureBuffer);
        pushConsts.a = kExposureScale;
        pushConsts.b = kExposureOffset;

        pushConsts.lowPercentage = math::clamp(config.autoExposureLowPercent, 0.01f, 0.99f);
        pushConsts.highPercentage = math::clamp(config.autoExposureHighPercent, 0.01f, 0.99f);

        pushConsts.minBrightness = math::exp2(config.autoExposureMinBrightness);
        pushConsts.maxBrightness = math::exp2(config.autoExposureMaxBrightness);

        pushConsts.compensation = math::clamp(config.autoExposureExposureCompensation, 0.1f, 1.0f);
        pushConsts.speedDown = config.autoExposureSpeedDown;
        pushConsts.speedUp = config.autoExposureSpeedUp;
        pushConsts.deltaTime = deltaTime;

        pushConsts.fixExposure = config.bAutoExposureEnable ? -1.0f : config.autoExposureFixExposure;
        auto computeShader = getContext().getShaderLibrary().getShader<AutoExposureCS>();
        addComputePass2(queue,
            "AutoExposure",
            getContext().computePipe(computeShader, "AutoExposure"),
            pushConsts,
            { 1, 1, 1 });
    }

    {
        ApplyExposurePushConsts pushConsts{};
        uint2 dim = { color->get().getExtent().width, color->get().getExtent().height };

        pushConsts.workDim = dim;
        pushConsts.UAV = asUAV(queue, color);
        pushConsts.SRV_exposure = asSRV(queue, exposureBuffer);
        
        const uint2 dispatchDim = divideRoundingUp(dim, uint2(32));

        auto computeShader = getContext().getShaderLibrary().getShader<ApplyExposureCS>();
        addComputePass2(queue,
            "ApplyExposure",
            getContext().computePipe(computeShader, "ApplyExposure"),
            pushConsts,
            { dispatchDim.x, dispatchDim.y, 1 });
    }

    return exposureBuffer;
}