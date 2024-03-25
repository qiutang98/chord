#pragma once

#include "binding.h"

// Blog: https://www.lei.chat/posts/hlsl-for-vulkan-resources/
// Type alias table between HLSL and GLSL.
/*
    HLSL Type	           DirectX Descriptor Type	Vulkan Descriptor Type	GLSL Type
    SamplerState	        Sampler	                 Sampler	             uniform sampler*
    SamplerComparisonState	Sampler	                 Sampler	             uniform sampler*Shadow
    Buffer	                SRV	                     Uniform Texel Buffer	 uniform samplerBuffer
    RWBuffer	            UAV	                     Storage Texel Buffer	 uniform imageBuffer
    Texture*	            SRV	                     Sampled Image	         uniform texture*
    RWTexture*	            UAV	                     Storage Image	         uniform image*
    cbuffer	                CBV	                     Uniform Buffer      	 uniform { ... }
    ConstantBuffer	        CBV	                     Uniform Buffer	         uniform { ... }
    tbuffer	                CBV	                     Storage Buffer
    TextureBuffer	        CBV	                     Storage Buffer
    StructuredBuffer	    SRV	                     Storage Buffer	         buffer { ... }
    RWStructuredBuffer	    UAV	                     Storage Buffer	         buffer { ... }
    ByteAddressBuffer	    SRV	                     Storage Buffer
    RWByteAddressBuffer	    UAV	                     Storage Buffer
    AppendStructuredBuffer	UAV	                     Storage Buffer
    ConsumeStructuredBuffer	UAV	                     Storage Buffer
*/

// NOTE: Current Spir-V still don't support ResourceDescriptorHeap. 
//       So need tons of macro to support fully bindless :(
#define T_BINDLESS_NAMED_RESOURCE(Type, DataType) CHORD_T_BINDLESS##Type##DataType
#define   BINDLESS_NAMED_RESOURCE(Type) CHORD_BINDLESS##Type

#define T_BINDLESS_DECLARE(Type, Binding, DataType) [[vk::binding(Binding, (int)ESetSlot::Bindless)]] Type<DataType> T_BINDLESS_NAMED_RESOURCE(Type, DataType)[];
#define   BINDLESS_DECLARE(Type, Binding) [[vk::binding(Binding, (int)ESetSlot::Bindless)]] Type BINDLESS_NAMED_RESOURCE(Type)[];

// Helper macro to load all template type.
#define TBindless(Type, DataType, Index) T_BINDLESS_NAMED_RESOURCE(Type, DataType)[NonUniformResourceIndex(Index)]
#define  Bindless(Type, Index) BINDLESS_NAMED_RESOURCE(Type)[NonUniformResourceIndex(Index)]


#define T_BINDLESS_TEXTURE_FORMAT_DECLARE(Type, Binding) \
    T_BINDLESS_DECLARE(Type, Binding, float ) \
    T_BINDLESS_DECLARE(Type, Binding, float2) \
    T_BINDLESS_DECLARE(Type, Binding, float3) \
    T_BINDLESS_DECLARE(Type, Binding, float4) \
    T_BINDLESS_DECLARE(Type, Binding, uint  ) \
    T_BINDLESS_DECLARE(Type, Binding, uint2 ) \
    T_BINDLESS_DECLARE(Type, Binding, uint3 ) \
    T_BINDLESS_DECLARE(Type, Binding, uint4 ) \
    T_BINDLESS_DECLARE(Type, Binding, int   ) \
    T_BINDLESS_DECLARE(Type, Binding, int2  ) \
    T_BINDLESS_DECLARE(Type, Binding, int3  ) \
    T_BINDLESS_DECLARE(Type, Binding, int4  ) \

T_BINDLESS_TEXTURE_FORMAT_DECLARE(Texture2D,   (int)EBindingType::SampledImage)
T_BINDLESS_TEXTURE_FORMAT_DECLARE(Texture3D,   (int)EBindingType::SampledImage)
T_BINDLESS_TEXTURE_FORMAT_DECLARE(TextureCube, (int)EBindingType::SampledImage)
T_BINDLESS_TEXTURE_FORMAT_DECLARE(RWTexture2D, (int)EBindingType::StorageImage)
T_BINDLESS_TEXTURE_FORMAT_DECLARE(RWTexture3D, (int)EBindingType::StorageImage)

#undef T_BINDLESS_TEXTURE_FORMAT_DECLARE

#define T_BINDLESS_STRUCTURED_BUFFER_DECLARE(Type) T_BINDLESS_DECLARE(StructuredBuffer, (int)EBindingType::StorageBuffer, Type)

// NOTE: I event don't understand why dxc still don't support bindless RW buffer :(
// https://github.com/microsoft/DirectXShaderCompiler/issues/5440
// [[vk::binding((int)EBindingType::StorageBuffer, (int)ESetSlot::Bindless)]] RWStructuredBuffer<Type> BindlessRWStructuredBuffer##Type[];

#define T_BINDLESS_CONSTATNT_BUFFER_DECLARE(Type) T_BINDLESS_DECLARE(ConstantBuffer, (int)EBindingType::UniformBuffer, Type)

T_BINDLESS_STRUCTURED_BUFFER_DECLARE(float)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(float2)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(float3)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(float4)

T_BINDLESS_STRUCTURED_BUFFER_DECLARE(int)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(int2)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(int3)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(int4)

T_BINDLESS_STRUCTURED_BUFFER_DECLARE(uint)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(uint2)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(uint3)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(uint4)

// ByteAddressBuffer don't care type. 
BINDLESS_DECLARE(ByteAddressBuffer, (int)EBindingType::StorageBuffer)
BINDLESS_DECLARE(RWByteAddressBuffer, (int)EBindingType::StorageBuffer)

// SamplerState don't care type.
BINDLESS_DECLARE(SamplerState, (int)EBindingType::Sampler)
BINDLESS_DECLARE(SamplerComparisonState, (int)EBindingType::Sampler)