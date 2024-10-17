#ifndef SHADER_BINDLESS_HLSLI
#define SHADER_BINDLESS_HLSLI

#include "binding.h"
#include "base.h"

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

// Declare of type.
#define T_BINDLESS_DECLARE(Type, Binding, DataType) [[vk::binding(Binding, 0)]] Type<DataType> T_BINDLESS_NAMED_RESOURCE(Type, DataType)[];
#define   BINDLESS_DECLARE(Type, Binding) [[vk::binding(Binding, 0)]] Type BINDLESS_NAMED_RESOURCE(Type)[];

/////////////////////////////////////////////////////////////////////////////////////////
// Texture area.
#define T_BINDLESS_TEXTURE_FORMAT_DECLARE(Type, Binding) \
    T_BINDLESS_DECLARE(Type, Binding, float ) \
    T_BINDLESS_DECLARE(Type, Binding, float1) \
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
    T_BINDLESS_DECLARE(Type, Binding, int4  ) 

T_BINDLESS_TEXTURE_FORMAT_DECLARE(Texture2D,   (int)chord::EBindingType::BindlessSampledImage)
T_BINDLESS_TEXTURE_FORMAT_DECLARE(Texture3D,   (int)chord::EBindingType::BindlessSampledImage)
T_BINDLESS_TEXTURE_FORMAT_DECLARE(TextureCube, (int)chord::EBindingType::BindlessSampledImage)
T_BINDLESS_TEXTURE_FORMAT_DECLARE(RWTexture2D, (int)chord::EBindingType::BindlessStorageImage)
T_BINDLESS_TEXTURE_FORMAT_DECLARE(RWTexture3D, (int)chord::EBindingType::BindlessStorageImage)

#undef T_BINDLESS_TEXTURE_FORMAT_DECLARE
/////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////
// Current no support.
/******************* UniformTexelBuffer & StorageTexelBuffer *******************
// RWBuffer/Buffer area. 
#define T_BINDLESS_BUFFER_DECLARE(Type)  \
    T_BINDLESS_DECLARE(Buffer,   (int)chord::EBindingType::BindlessUniformTexelBuffer, Type) \
    T_BINDLESS_DECLARE(RWBuffer, (int)chord::EBindingType::BindlessStorageTexelBuffer, Type)

T_BINDLESS_BUFFER_DECLARE(float)
T_BINDLESS_BUFFER_DECLARE(float2)
T_BINDLESS_BUFFER_DECLARE(float3)
T_BINDLESS_BUFFER_DECLARE(float4)

// Int type structural buffer declare.
T_BINDLESS_BUFFER_DECLARE(int)
T_BINDLESS_BUFFER_DECLARE(int2)
T_BINDLESS_BUFFER_DECLARE(int3)
T_BINDLESS_BUFFER_DECLARE(int4)

// Uint type structural buffer declare.
T_BINDLESS_BUFFER_DECLARE(uint)
T_BINDLESS_BUFFER_DECLARE(uint2)
T_BINDLESS_BUFFER_DECLARE(uint3)
T_BINDLESS_BUFFER_DECLARE(uint4)

#undef T_BINDLESS_BUFFER_DECLARE
***************************************************************************************/
////////////////////////////////////////////////////////////////////////////////////////

#define T_BINDLESS_CONSTATNT_BUFFER_DECLARE(Type) \
    T_BINDLESS_DECLARE(ConstantBuffer, (int)chord::EBindingType::BindlessUniformBuffer, Type)

////////////////////////////////////////////////////////////////////////////////////////
// Current no support.
/******************* StructureBuffer & RWStructureBuffer *******************
#define T_BINDLESS_STRUCTURED_BUFFER_DECLARE(Type) \
    T_BINDLESS_DECLARE(StructuredBuffer,   (int)chord::EBindingType::BindlessStorageBuffer, Type) \
    T_BINDLESS_DECLARE(RWStructuredBuffer, (int)chord::EBindingType::BindlessStorageBuffer, Type) 

// Float type structural buffer declare.
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(float)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(float2)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(float3)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(float4)

// Int type structural buffer declare.
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(int)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(int2)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(int3)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(int4)

// Uint type structural buffer declare.
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(uint)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(uint2)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(uint3)
T_BINDLESS_STRUCTURED_BUFFER_DECLARE(uint4)

#undef T_BINDLESS_STRUCTURED_BUFFER_DECLARE
***************************************************************************************/
////////////////////////////////////////////////////////////////////////////////////////

// ByteAddressBuffer don't care type. 
BINDLESS_DECLARE(ByteAddressBuffer, (int)chord::EBindingType::BindlessStorageBuffer)
BINDLESS_DECLARE(RWByteAddressBuffer, (int)chord::EBindingType::BindlessStorageBuffer)

// SamplerState don't care type.
BINDLESS_DECLARE(SamplerState, (int)chord::EBindingType::BindlessSampler)
BINDLESS_DECLARE(SamplerComparisonState, (int)chord::EBindingType::BindlessSampler)

// Helper macro to load all template type.
#define TBindless(Type, DataType, Index) T_BINDLESS_NAMED_RESOURCE(Type, DataType)[NonUniformResourceIndex(Index)]
#define  Bindless(Type, Index) BINDLESS_NAMED_RESOURCE(Type)[NonUniformResourceIndex(Index)]

#define ByteAddressBindless(Index) Bindless(ByteAddressBuffer, Index)
#define RWByteAddressBindless(Index) Bindless(RWByteAddressBuffer, Index)


// Usage:
//
// TBindless(ConstantBuffer, ...)
// ByteAddressBindless()

#define TypeLoad(Type, ElementId) Load<Type>((ElementId) * sizeof(Type))
#define TypeStore(Type, ElementId, Value) Store<Type>((ElementId) * sizeof(Type), Value)

#define BATL(Type, BufferId, ElementId) ByteAddressBindless(BufferId).TypeLoad(Type, ElementId)
#define BATS(Type, BufferId, ElementId, Value) RWByteAddressBindless(BufferId).TypeStore(Type, ElementId, Value)
 
#define LoadCameraView(Index) BATL(PerframeCameraView, Index, 0)


uint interlockedAddUint(uint bufferId, uint count = 1)
{
    uint index;
    RWByteAddressBuffer bufferAcess = RWByteAddressBindless(bufferId);
    bufferAcess.InterlockedAdd(0, count, index);
    return index;
}

SamplerState getPointClampEdgeSampler(PerframeCameraView perView)
{
    SamplerState pointClampSampler = Bindless(SamplerState, perView.basicData.pointClampEdgeSampler);
    return pointClampSampler;
}

SamplerState getLinearClampEdgeSampler(PerframeCameraView perView)
{
    SamplerState linearClampSampler = Bindless(SamplerState, perView.basicData.linearClampEdgeSampler);
    return linearClampSampler;
}

#define  storeRWTexture2D_Declare(Type) \
    void storeRWTexture2D_##Type(uint id, uint2 pos, Type v) { RWTexture2D<Type> rw = TBindless(RWTexture2D, Type, id); rw[pos] = v; }

storeRWTexture2D_Declare(float4)
storeRWTexture2D_Declare(float3)
storeRWTexture2D_Declare(float2)
storeRWTexture2D_Declare(float1)

#undef storeRWTexture2D_Declare

#define loadTexture2D_Declare(Type) \
    Type loadTexture2D_##Type(uint id, uint2 pos) { Texture2D<Type> r = TBindless(Texture2D, Type, id); return r[pos];  }

loadTexture2D_Declare(float4)
loadTexture2D_Declare(float3)
loadTexture2D_Declare(float2)
loadTexture2D_Declare(float1)

#undef loadTexture2D_Declare

#define sampleTexture2D_Declare(Type) \
    Type sampleTexture2D_##Type(uint id, float2 uv, SamplerState s, int lod = 0) { Texture2D<Type> r = TBindless(Texture2D, Type, id); return r.SampleLevel(s, uv, lod);  }

sampleTexture2D_Declare(float4)
sampleTexture2D_Declare(float3)
sampleTexture2D_Declare(float2)
sampleTexture2D_Declare(float1)

#undef sampleTexture2D_Declare

#endif // !SHADER_BINDLESS_HLSLI