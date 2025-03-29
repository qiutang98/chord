#ifndef SHADER_MESH_RASTER_HLSL
#define SHADER_MESH_RASTER_HLSL

#include "gltf.h"

#define PASS_TYPE_CLUSTER 0
#define PASS_TYPE_DEPTH   1

struct MeshRasterPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(MeshRasterPushConst);
    uint cameraViewId;
    uint drawedMeshletCmdId;

    uint instanceViewId;
    uint instanceViewOffset;
};
CHORD_PUSHCONST(MeshRasterPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "nanite_shared.hlsli"

struct MeshRasterPassMS2PS
{
    float4 positionHS : SV_Position;

#if DIM_PASS_TYPE == PASS_TYPE_CLUSTER
    nointerpolation uint clusterId : TEXCOORD0;
#endif

#if DIM_MASKED_MATERIAL
    float2 uv : TEXCOORD1;
    nointerpolation uint2 textureSamplerId : TEXCOORD2;
    float2 alphaMisc : TEXCOORD3;
#endif
};

struct PrimitiveAttributes
{
    uint primitiveId : SV_PrimitiveID;
    bool bCulled   : SV_CullPrimitive;
};

groupshared float sharedVerticesHS_R[kNaniteMeshletMaxVertices];
groupshared float sharedVerticesHS_G[kNaniteMeshletMaxVertices];
groupshared float sharedVerticesHS_B[kNaniteMeshletMaxVertices];

[numthreads(kMeshShaderTGSize, 1, 1)]
[outputtopology("triangle")]
void meshRasterPassMS(
    uint dispatchThreadId : SV_DispatchThreadID,
    uint groupThreadId : SV_GroupThreadID,
    uint groupId : SV_GroupID,
    out vertices MeshRasterPassMS2PS verts[kNaniteMeshletMaxVertices],
    out primitives PrimitiveAttributes prims[kNaniteMeshletMaxTriangle],
    out indices  uint3 tris[kNaniteMeshletMaxTriangle])
{
    const InstanceCullingViewInfo instanceView = BATL(InstanceCullingViewInfo, pushConsts.instanceViewId, pushConsts.instanceViewOffset);


    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint3 drawCmd = BATL(uint3, pushConsts.drawedMeshletCmdId, groupId);
    uint objectId  = drawCmd.x;
    uint meshletId = drawCmd.y;

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);
    const GLTFMaterialGPUData materialInfo = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer meshletDataBuffer  = ByteAddressBindless(primitiveDataInfo.meshletDataBuffer);

    const uint verticesCount = unpackVertexCount(meshlet.vertexTriangleCount);
    const uint trianglesCount = unpackTriangleCount(meshlet.vertexTriangleCount);

    SetMeshOutputCounts(verticesCount, trianglesCount);
    for (uint i = groupThreadId; i < verticesCount; i += kMeshShaderTGSize)
    {
        // Noew get vertices id. 
        const uint verticesSampleOffset = meshlet.dataOffset + i;
        const uint verticesIndex = meshletDataBuffer.TypeLoad(uint, verticesSampleOffset);
        const uint indicesId = primitiveInfo.vertexOffset + verticesIndex;

        const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
        const float4x4 mvp = mul(instanceView.translatedWorldToClip, localToTranslatedWorld);

        const float3 positionLS = positionDataBuffer.TypeLoad(float3, indicesId);

        MeshRasterPassMS2PS output;

        // Get HS position.
        const float4 positionHS = mul(mvp, float4(positionLS, 1.0));
        output.positionHS = positionHS;

        // Store in shared memory for triangle culling.
        sharedVerticesHS_R[i] = positionHS.x;
        sharedVerticesHS_G[i] = positionHS.y;
        sharedVerticesHS_B[i] = positionHS.w;

    #if DIM_MASKED_MATERIAL
        ByteAddressBuffer uvDataBuffer = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);
        output.uv = uvDataBuffer.TypeLoad(float2, indicesId); //
        output.textureSamplerId = uint2(materialInfo.baseColorId, materialInfo.baseColorSampler);
        output.alphaMisc = float2(materialInfo.baseColorFactor.w, materialInfo.alphaCutOff);
    #endif

    #if DIM_PASS_TYPE == PASS_TYPE_CLUSTER
        output.clusterId = drawCmd.z;
    #endif
    
        verts[i] = output;
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint i = groupThreadId; i < trianglesCount; i += kMeshShaderTGSize)
    {
        uint triangleIndicesSampleOffset = meshlet.dataOffset + verticesCount + i;
        uint indexTri = meshletDataBuffer.TypeLoad(uint, triangleIndicesSampleOffset);

        uint3 triangleIndices;
        triangleIndices.x = (indexTri >> (0 * 8)) & 0xff;
        triangleIndices.y = (indexTri >> (1 * 8)) & 0xff;
        triangleIndices.z = (indexTri >> (2 * 8)) & 0xff;

        // Fill triangles.
        tris[i] = triangleIndices;

        const float3 positionHS_0 = float3(sharedVerticesHS_R[triangleIndices.x], sharedVerticesHS_G[triangleIndices.x], sharedVerticesHS_B[triangleIndices.x]);
        const float3 positionHS_1 = float3(sharedVerticesHS_R[triangleIndices.y], sharedVerticesHS_G[triangleIndices.y], sharedVerticesHS_B[triangleIndices.y]);
        const float3 positionHS_2 = float3(sharedVerticesHS_R[triangleIndices.z], sharedVerticesHS_G[triangleIndices.z], sharedVerticesHS_B[triangleIndices.z]);

        bool bCulled = false;

        // #0. Back face culling. 
        #if !DIM_TWO_SIDED
        if (!bCulled)
        {
            // https://www.cs.cmu.edu/afs/cs/academic/class/15869-f11/www/readings/olano97_homogeneous.pdf
            bCulled = determinant(float3x3(positionHS_0, positionHS_1, positionHS_2)) <= 0;
        }
        #endif

        // #1. Near plane culling.
        if (!bCulled)
        {
            bCulled = (positionHS_0.z <= 0 && positionHS_1.z <= 0 && positionHS_2.z <= 0);
        }

        // Now cast to screen space.
        // Neagtive w flip and any triangle that straddles the plane won't be projected onto two sides of the screen
        const float2 uv_0 = positionHS_0.xy / abs(positionHS_0.z) * float2(0.5, -0.5) + 0.5;
        const float2 uv_1 = positionHS_1.xy / abs(positionHS_1.z) * float2(0.5, -0.5) + 0.5;
        const float2 uv_2 = positionHS_2.xy / abs(positionHS_2.z) * float2(0.5, -0.5) + 0.5; // No fully ndc culling, just use .xy

        // UV AABB.
        const float2 maxUv = max(uv_0, max(uv_1, uv_2));
        const float2 minUv = min(uv_0, min(uv_1, uv_2));

        // #2. Frustum culling.
        if (!bCulled)
        {
            bCulled = any(minUv >= 1) || any(maxUv <= 0);
        }

        // #3. Small primitive culling.
        if (!bCulled)
        {
            const float2 maxScreenPosition = maxUv * instanceView.renderDimension.xy;
            const float2 minScreenPosition = minUv * instanceView.renderDimension.xy;
            bCulled = any(round(minScreenPosition) == round(maxScreenPosition));
        }

        // Fill primitive attribute.
        prims[i].primitiveId = i;
        prims[i].bCulled = bCulled;
    }
}

void meshRasterPassPS(
      in uint primitiveId : SV_PrimitiveID
    , in MeshRasterPassMS2PS input
#if DIM_PASS_TYPE == PASS_TYPE_CLUSTER
    , out uint outId : SV_Target0
#else 
    // Default output for compiler.
    , out float4 dummy : SV_Target0
#endif
)
{
#if DIM_MASKED_MATERIAL
    Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, input.textureSamplerId.x);
    SamplerState baseColorSampler = Bindless(SamplerState, input.textureSamplerId.y);

    float4 sampleColor = baseColorTexture.Sample(baseColorSampler, input.uv);
    clip(sampleColor.w * input.alphaMisc.x - input.alphaMisc.y);
#endif

#if DIM_PASS_TYPE == PASS_TYPE_CLUSTER
    // Output id.
    outId = encodeTriangleIdInstanceId(primitiveId, input.clusterId);
#endif
}

#endif

#endif // SHADER_MESH_RASTER_HLSL