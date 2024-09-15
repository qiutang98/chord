#ifndef NANITE_SHARED_HLSL
#define NANITE_SHARED_HLSL

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"

struct TriangleMiscInfo
{
    float4  positionHS[3];
    float3   tangentRS[3];
    float3 bitangentRS[3];
    float3    normalRS[3];
    float2          uv[3];
};

GPUGLTFMeshlet getTriangleMiscInfo(
    in const PerframeCameraView perView,
    in const GPUBasicData scene,
    in const GPUObjectGLTFPrimitive objectInfo,
    in const bool bExistNormalTexture,
    in const float4x4 localToClip,
    in const uint meshletId, 
    in const uint triangleId,
    out TriangleMiscInfo outTriangle,
    out uint traingleIndexId)
{
    const GLTFPrimitiveBuffer primitiveInfo          = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet                     = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer meshletDataBuffer = ByteAddressBindless(primitiveDataInfo.meshletDataBuffer);
    uint verticesCount                  = unpackVertexCount(meshlet.vertexTriangleCount);
    uint triangleIndicesSampleOffset    = (meshlet.dataOffset + verticesCount + triangleId) * 4;
    uint indexTri                       = meshletDataBuffer.Load(triangleIndicesSampleOffset);

    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer normalBuffer  = ByteAddressBindless(primitiveDataInfo.normalBuffer);
    ByteAddressBuffer uvDataBuffer  = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);
    ByteAddressBuffer tangentBuffer = ByteAddressBindless(primitiveDataInfo.tangentBuffer);

    [unroll(3)]
    for(uint i = 0; i < 3; i ++)
    {
        const uint verticesSampleOffset = (meshlet.dataOffset + ((indexTri >> (i * 8)) & 0xff)) * 4;
        const uint indicesId = primitiveInfo.vertexOffset + meshletDataBuffer.Load(verticesSampleOffset);

        // position load and projection.
        {
            const float3 positionLS = positionDataBuffer.TypeLoad(float3, indicesId);
            outTriangle.positionHS[i] = mul(localToClip, float4(positionLS, 1.0));
        }

        // Load uv. 
        outTriangle.uv[i] =  uvDataBuffer.TypeLoad(float2, indicesId);

        // Non-uniform scale need invert local to world matrix's transpose.
        // see http://www.lighthouse3d.com/tutorials/glsl-12-tutorial/the-normal-matrix/.
        const float3 normalLS = normalBuffer.TypeLoad(float3, indicesId);
        outTriangle.normalRS[i] = normalize(mul(float4(normalLS, 0.0), objectInfo.basicData.translatedWorldToLocal).xyz);

        [branch]
        if (bExistNormalTexture)
        {
            // Tangent direction don't care about non-uniform scale.
            // see http://www.lighthouse3d.com/tutorials/glsl-12-tutorial/the-normal-matrix/.
            const float4 tangentLS = tangentBuffer.TypeLoad(float4, indicesId);
            outTriangle.tangentRS[i] = mul(objectInfo.basicData.localToTranslatedWorld, float4(tangentLS.xyz, 0.0)).xyz;

            // Gram-Schmidt re-orthogonalize. https://learnopengl.com/Advanced-Lighting/Normal-Mapping
            outTriangle.tangentRS[i] = normalize(outTriangle.tangentRS[i] - dot(outTriangle.tangentRS[i], outTriangle.normalRS[i]) * outTriangle.normalRS[i]);

            // Then it's easy to compute bitangent now.
            // tangentLS.w = sign(dot(normalize(bitangent), normalize(cross(normal, tangent))));
            outTriangle.bitangentRS[i] = cross(outTriangle.normalRS[i], outTriangle.tangentRS[i]) * tangentLS.w;
        }
    }

    // Outputs.
    traingleIndexId = indexTri;
    return meshlet;
}

#endif