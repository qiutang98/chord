#ifndef SHADER_BASE_H
#define SHADER_BASE_H

#ifdef __cplusplus /////////////////////////////////////////
    
    #include <utils/utils.h>

    using namespace chord;
    using namespace chord::math;

    // Type alias bool.
    using bool2 = chord::math::bvec2;
    using bool3 = chord::math::bvec3;
    using bool4 = chord::math::bvec4;

    // Type alias uint.
    using uint  = chord::uint32;
    using uint2 = chord::math::uvec2;
    using uint3 = chord::math::uvec3;
    using uint4 = chord::math::uvec4;

    // Type alias int.
    using int2 = chord::math::ivec2;
    using int3 = chord::math::ivec3;
    using int4 = chord::math::ivec4;

    // Type alias floats.
    using float2 = chord::math::vec2;
    using float3 = chord::math::vec3;
    using float4 = chord::math::vec4;

    // Type alias matrix.
    using float2x2 = chord::math::mat2;
    using float3x3 = chord::math::mat3;
    using float4x4 = chord::math::mat4;

    namespace chord
    {
        constexpr auto kMaxPushConstSize = 128;

        inline float4 lerp(const float4& x, const float4& y, const float4& a) { return math::mix(x, y, a); }
        inline float3 lerp(const float3& x, const float3& y, const float3& a) { return math::mix(x, y, a); }
        inline float3 lerp(const float3& x, const float3& y, const float   a) { return math::mix(x, y, a); }
        inline float2 lerp(const float2& x, const float2& y, const float2& a) { return math::mix(x, y, a); }
        inline float  lerp(const float   x, const float   y, const float   a) { return math::mix(x, y, a); }

        inline float3 pow(const float3& base, float v)
        {
            return math::pow(base, float3(v));
        }

        inline float pow(const float base, float v)
        {
            return math::pow(base, v);
        }
    }

    // Cpp end check struct size match machine.
    #define CHORD_PUSHCONST(Type, Name) \
        static_assert(sizeof(Type) <= chord::kMaxPushConstSize)

    // Serialization relative.
    #define CHORD_DEFAULT_COMPARE_ARCHIVE(T)       \
        ARCHIVE_DECLARE                            \
        bool operator==(const T&) const = default;
        
    // GPU size require struct size pad 4 float.
    #define CHORD_CHECK_SIZE_GPU_SAFE(X) \
        static_assert(sizeof(X) % (4 * sizeof(float)) == 0)

    static inline float asuint(float floatValue)
    {
        return *reinterpret_cast<uint*>(&floatValue);
    }

    static inline float asfloat(uint32 uintValue)
    {
        return *reinterpret_cast<float*>(&uintValue);
    }

#else  ///////////////////////////////////////////////////////

    // HLSL is declaration pushconst.
    #define CHORD_PUSHCONST(Type, Name) [[vk::push_constant]] Type Name
    #define CHORD_DEFAULT_COMPARE_ARCHIVE(T)
    #define CHORD_CHECK_SIZE_GPU_SAFE(X)
    #define ARCHIVE_DECLARE

#endif ///////////////////////////////////////////////////////

struct GPUBasicData
{
    uint frameCounter; // 32 bit-framecounter, easy overflow. todo:
    uint frameCounterMod8;
    uint GLTFPrimitiveDetailBuffer; // gpu scene: gltf primitive detail buffer index.
    uint GLTFPrimitiveDataBuffer; // gpu scene: gltf primitive datas buffer index.
    
    uint GLTFMaterialBuffer; 
    uint GLTFObjectCount; // scene: total gltf object count.
    uint GLTFObjectBuffer; // scene: gltf object buffer index.
    uint debuglineVertices; // debugline store vertices buffer index.
    
    uint debuglineCount; // current debug line use count buffer index.
    uint debuglineMaxCount; // maximum of debug line can use.
    uint pointClampEdgeSampler; // All point clamp edge sampler. 
    uint pad2;
};

    

struct PerframeCameraView
{
    GPUBasicData basicData;

    // float4x4 localToClip = mul(mul(viewToClip, translatedWorldToView), localToTranslatedWorld);
    float4x4 translatedWorldToView;
    float4x4 viewToTranslatedWorld;

    float4x4 viewToClip;
    float4x4 clipToView;

    float4x4 translatedWorldToClip;
    float4x4 clipToTranslatedWorld;

    float4 frustumPlane[6]; // World space frustum plane.

    float4x4 translatedWorldToClipLastFrame;
    float4 frustumPlaneLastFrame[6];

    float4 renderDimension; //  .xy is width and height, .zw is inverse of .xy.

    float4 camMiscInfo; // .x = fovy,
};
CHORD_CHECK_SIZE_GPU_SAFE(PerframeCameraView);

// Per-object descriptor in GPU, collected every frame.
struct GPUObjectBasicData
{
    float4x4 localToTranslatedWorld;
    float4x4 translatedWorldToLocal; // inverse of localToTranslatedWorld

    float4x4 localToTranslatedWorldLastFrame;

    float4 scaleExtractFromMatrix; // .xyz is scale, .w is largest abs(.xyz)
};

struct GPUObjectGLTFPrimitive
{
    GPUObjectBasicData basicData;
    uint GLTFPrimitiveDetail;
    uint GLTFMaterialData;
    uint pad1;
    uint pad2;
};

inline uint shaderSetFlag(uint flags, uint bit)
{
    return flags | (1U << bit);
}

inline bool shaderHasFlag(uint flags, uint bit)
{
    return (flags & (1U << bit)) != 0U;
}

struct LineDrawVertex
{
    float3 translatedWorldPos;
    uint color; // 8 bit per component, .rgba
};



inline uint shaderPackColor(uint R, uint G, uint B, uint A)
{
    uint color = 0;
    color |= (R & 0xFF) <<  0;
    color |= (G & 0xFF) <<  8;
    color |= (B & 0xFF) << 16;
    color |= (A & 0xFF) << 24;
    return color;
}

inline uint shaderPackColor(uint4 c)
{
    return shaderPackColor(c.x, c.y, c.z, c.w);
}

inline float4 shaderUnpackColor(uint packData)
{
    float4 color;
    color.r = float((packData >>  0) & 0xFF) / 255.0;
    color.g = float((packData >>  8) & 0xFF) / 255.0;
    color.b = float((packData >> 16) & 0xFF) / 255.0;
    color.a = float((packData >> 24) & 0xFF) / 255.0;

    return color;
}

// HZB mipmap count max 12, meaning from 4096 - 1.
#define kHZBMaxMipmapCount 12

// 24 bit for max object id count.
#define kMaxObjectCount 0xFFFFFF

// 25 bit for max meshlet id count.
#define kMaxMeshletCount 0x1FFFFFF

// Max shading type is 127.
#define kMaxShadingType  0x7F

// Shading type list, max is 127.
enum class EShadingType
{
    None = 0,
    GLTF_MetallicRoughnessPBR = 1,

    MAX
};

// LIGHTING_TYPE
#define kLightingType_None 0
#define kLightingType_GLTF_MetallicRoughnessPBR 1

#define kWaveSize 32

// Nanite config: fit mesh shader.
#define kNaniteMeshletMaxVertices 64
#define kNaniteMeshletMaxTriangle 126
#define kNaniteMaxLODCount        8

struct NaniteShadingMeshlet
{
    uint shadingPack; // ShadingType: 8bit, Material Id: 24bit.
};


 // smallest such that 1.0 + kEpsilon != 1.0
#define kEpsilon 1.192092896e-07F

// 
#define kUnvalidIdUint32 0xFFFFFFFFu

// Draw command for gltf meshlet, used for vertex shader fallback.
struct GLTFMeshletDrawCmd
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
    uint objectId;
    uint meshletId;
};

#endif // !SHADER_BASE_H