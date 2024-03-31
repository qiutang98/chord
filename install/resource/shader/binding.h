#pragma once

namespace chord
{
    // Whole application used one set which host in BindlessManager.
    // 
    enum class EBindingType
    {
        // StructuredBuffer<T>
        // RWStructuredBuffer<T>
        // ByteAddressBuffer
        // RWByteAddressBuffer
        // RWBuffer<T>
        // Buffer<T>
        BindlessStorageBuffer = 0, 

        // ConstantBuffer<T>
        BindlessUniformBuffer,

        // Texture2D<T>
        // Texture3D<T>
        // TextureCube<T>
        BindlessSampledImage, 

        // RWTexture2D<T>
        // RWTexture3D<T>
        BindlessStorageImage, 

        // SamplerState
        // SamplerComparisonState
        BindlessSampler,   

        MAX
    };
}