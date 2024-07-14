#ifndef SHADER_BINDING_H
#define SHADER_BINDING_H

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

        // Buffer<T>
        // BindlessUniformTexelBuffer,

        // RWBuffer<T>
        // BindlessStorageTexelBuffer,

        MAX
    };
}

#endif // !SHADER_BINDING_H