# Chord - UE5风格的现代vulkan渲染引擎

0. GPU Scene与Full Bindless资源系统。
1. [Nanite风格的无缝网格LOD渲染。](https://qiutang98.github.io/post/%E5%AE%9E%E6%97%B6%E6%B8%B2%E6%9F%93%E5%BC%80%E5%8F%91/mynanite01_mesh_processor/)

![image](gallery/Nanite.png)

这是一个简洁的，模仿UE5代码风格的渲染引擎，用来做一些快速图形原型开发迭代。API风格和UE类似，便于测试完成后立刻移植到UE5引擎中（没有人想在UE5里不停的等待编译和等待加载 :D）。

一个简单Pass在Chord中的声明和使用非常简洁：

```C++
PRIVATE_GLOBAL_SHADER(TonemappingPS, "resource/shader/tonemapping.hlsl", "mainPS", EShaderStage::Pixel);

void chord::tonemapping(GraphicsQueue& queue, PoolTextureRef srcImage, PoolTextureRef outImage)
{
    RenderTargets RTs { };
    RTs.RTs[0] = RenderTargetRT(outImage, ERenderTargetLoadStoreOp::Nope_Store);

    uint32 srcImageId = asSRV(queue, srcImage);

    TonemappingPushConsts pushConst { };
    pushConst.textureId = srcImageId;
    pushConst.pointClampSamplerId = getSamplers().pointClampBorder0000().index.get();

    addFullScreenPass2<TonemappingPS>(queue, "Tonemapping", RTs, pushConst);
}
```

Shader变体管理风格与UE5相同，搭配同款UE5的CVar可以实时切换不同变体：

```C++
static uint32 sInstanceCullingEnableHZBCulling = 1;
static AutoCVarRef cVarInstanceCullingEnableHZBCulling(
    "r.instanceculling.hzbCulling",
    sInstanceCullingEnableHZBCulling,
    "Enable meshlet hzb culling or not."
);

class HZBCullCS : public GlobalShader
{
public:
    DECLARE_SUPER_TYPE(GlobalShader);

    class SV_bFirstStage : SHADER_VARIANT_BOOL("DIM_HZB_CULLING_PHASE_0");
    class SV_bPrintDebugBox : SHADER_VARIANT_BOOL("DIM_PRINT_DEBUG_BOX");
    using Permutation = TShaderVariantVector<SV_bFirstStage, SV_bPrintDebugBox>;
};
IMPLEMENT_GLOBAL_SHADER(HZBCullCS, "resource/shader/instance_culling.hlsl", "HZBCullingCS", EShaderStage::Compute);

void dispatch(...)
{
    // ...
    
    HZBCullCS::Permutation permutation;
    permutation.set<HZBCullCS::SV_bFirstStage>(bFirstStage);
    permutation.set<HZBCullCS::SV_bPrintDebugBox>(sInstanceCullingEnableHZBCulling != 0);

    auto computeShader = getContext().getShaderLibrary().getShader<HZBCullCS>(permutation);
    addIndirectComputePass2(queue,
        bFirstStage ? "HZBCulling: Stage#0" : "HZBCulling: Stage#1",
        getContext().computePipe(computeShader, "HZBCullingPipe"),
        pushConst,
        meshletCullCmdBuffer);
}
```

支持CVar指令Shader热重载，在Console面板中输入该指令即可重新加载该Shader文件：

```
r.shader.recompile resource/shader/instance_culling.hlsl
```

使用Pool自动跟踪GPU资源生命周期，可以像UE那样随心所欲的到处申请RT和Buffer：

```C++
// Count and cmd.
auto countBuffer = getContext().getBufferPool().createGPUOnly(
    "CountBuffer", 
    sizeof(uint), 
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | 
    VK_BUFFER_USAGE_TRANSFER_DST_BIT);

auto Texture = getContext().getTexturePool().create(
    "Gbuffer.Color", 
    width, 
    height, G
    BufferTextures::colorFormat(), 
    kGBufferVkImageUsage | VK_IMAGE_USAGE_STORAGE_BIT);
```

支持UE5风格的GPU Scene和Bindless，可以实验一些先进的渲染技术。
