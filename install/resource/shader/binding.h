#pragma once

enum class ESetSlot
{
    // Coomon bindless set.
    Bindless = 0,

    // Global perframe bind set.
    Perframe = 1,

    // Optional reflected push set, per pass.
    PerPass = 2,

    // Optional reflected push set, per batch.
    PerBatch = 3,

    MAX 
};

enum class EBindingType
{
    // StructuredBuffer<T>/RWStructuredBuffer<T>/ByteAddressBuffer/RWByteAddressBuffer
    // NOTE: DXC spir-V current no support bindless RWStructuredBuffer<T>.
    StorageBuffer = 0,

    // ConstantBuffer<T>
    UniformBuffer,

    // Texture2D/Texture3D/TextureCube
    SampledImage,

    // RWTexture2D<T>/RWTexture3D<T>
    StorageImage,

    // SamplerState/SamplerComparisonState
    Sampler,

    MAX
};