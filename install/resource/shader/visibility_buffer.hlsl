#include "gltf.h"

struct VisibilityBufferPushConst
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(VisibilityBufferPushConst);
    uint cameraViewId;
    uint drawedMeshletCmdId;
};
CHORD_PUSHCONST(VisibilityBufferPushConst, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"

struct VisibilityPassVS2PS
{
    float4 positionHS : SV_Position;
    nointerpolation uint2 id : TEXCOORD0;
#if DIM_MASKED_MATERIAL
    float2 uv : TEXCOORD1;
#endif
};

struct PrimitiveAttributes
{
    uint primitiveId : SV_PrimitiveID;
    bool bCulled   : SV_CullPrimitive;
};

groupshared float sharedVerticesHS_R[64];
groupshared float sharedVerticesHS_G[64];
groupshared float sharedVerticesHS_B[64];

[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void visibilityPassMS(
    uint dispatchThreadId : SV_DispatchThreadID,
    uint groupThreadId : SV_GroupThreadID,
    uint groupId : SV_GroupID,
    out vertices VisibilityPassVS2PS verts[kNaniteMeshletMaxVertices],
    out primitives PrimitiveAttributes prims[kNaniteMeshletMaxTriangle],
    out indices  uint3 tris[kNaniteMeshletMaxTriangle])
{
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint3 drawCmd = BATL(uint3, pushConsts.drawedMeshletCmdId, groupId);
    uint objectId  = drawCmd.x;
    uint meshletId = drawCmd.y;

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer meshletDataBuffer  = ByteAddressBindless(primitiveDataInfo.meshletDataBuffer);

    const uint verticesCount = unpackVertexCount(meshlet.vertexTriangleCount);
    const uint trianglesCount = unpackTriangleCount(meshlet.vertexTriangleCount);

    SetMeshOutputCounts(verticesCount, trianglesCount);
    for (uint i = groupThreadId; i < verticesCount; i += 64)
    {
        // Noew get vertices id. 
        const uint verticesSampleOffset = meshlet.dataOffset + i;
        const uint verticesIndex = meshletDataBuffer.TypeLoad(uint, verticesSampleOffset);
        const uint indicesId = primitiveInfo.vertexOffset + verticesIndex;

        float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
        float4x4 translatedWorldToClip = perView.translatedWorldToClip;

        const float3 positionLS = positionDataBuffer.TypeLoad(float3, indicesId);
        const float4 positionRS = mul(localToTranslatedWorld, float4(positionLS, 1.0));

        VisibilityPassVS2PS output;

        // Get HS position.
        const float4 positionHS = mul(translatedWorldToClip, positionRS);
        output.positionHS = positionHS;
        sharedVerticesHS_R[i] = positionHS.x;
        sharedVerticesHS_G[i] = positionHS.y;
        sharedVerticesHS_B[i] = positionHS.w;

    #if DIM_MASKED_MATERIAL
        ByteAddressBuffer uvDataBuffer = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);
        output.uv = uvDataBuffer.TypeLoad(float2, indicesId); //
    #endif

        output.id.x = drawCmd.z;
        output.id.y = objectId;

        verts[i] = output;
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint i = groupThreadId; i < trianglesCount; i += 64)
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
            const float2 maxScreenPosition = maxUv * perView.renderDimension.xy;
            const float2 minScreenPosition = minUv * perView.renderDimension.xy;
            bCulled = any(round(minScreenPosition) == round(maxScreenPosition));
        }

        // Fill primitive attribute.
        prims[i].primitiveId = i;
        prims[i].bCulled = bCulled;
    }
}

// TODO: CS build draw list.
void visibilityPassVS(
    uint vertexId : SV_VertexID, 
    uint instanceId : SV_INSTANCEID,
    out VisibilityPassVS2PS output)
{
    const GLTFMeshletDrawCmd drawCmd = BATL(GLTFMeshletDrawCmd, pushConsts.drawedMeshletCmdId, instanceId);
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;


    // Load object and meshlet.
    uint objectId  = drawCmd.objectId;
    uint meshletId = drawCmd.meshletId;
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);
    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);
    ByteAddressBuffer meshletDataBuffer = ByteAddressBindless(primitiveDataInfo.meshletDataBuffer);

    // Get current triangle local id.
    const uint triangleId = vertexId / 3;

    // Get vertices count. 
    uint verticesCount = unpackVertexCount(meshlet.vertexTriangleCount);

    // Get meshlet index of triangle. 
    uint triangleIndicesSampleOffset = (meshlet.dataOffset + verticesCount + triangleId) * 4;
    uint indexTri = meshletDataBuffer.Load(triangleIndicesSampleOffset);

    // Noew get vertices id. 
    uint verticesSampleOffset = (meshlet.dataOffset + ((indexTri >> ((vertexId % 3) * 8)) & 0xff)) * 4;
    uint verticesIndex = meshletDataBuffer.Load(verticesSampleOffset);
    const uint indicesId = primitiveInfo.vertexOffset + verticesIndex;

    // 
    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    float4x4 translatedWorldToClip = perView.translatedWorldToClip;

    const float3 positionLS = positionDataBuffer.TypeLoad(float3, indicesId);
    const float4 positionRS = mul(localToTranslatedWorld, float4(positionLS, 1.0));

    output.positionHS = mul(translatedWorldToClip, positionRS);
    
#if DIM_MASKED_MATERIAL
    ByteAddressBuffer uvDataBuffer = ByteAddressBindless(primitiveDataInfo.textureCoord0Buffer);
    output.uv = uvDataBuffer.TypeLoad(float2, indicesId); //
#endif

    output.id.x = drawCmd.instanceId;
    output.id.y = objectId;
} 

void visibilityPassPS(
    in uint primitiveId : SV_PrimitiveID,
    in VisibilityPassVS2PS input, 
    out uint outId : SV_Target0)
{
    uint objectId = input.id.y;

#if DIM_MASKED_MATERIAL
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFMaterialGPUData materialInfo = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    Texture2D<float4> baseColorTexture = TBindless(Texture2D, float4, materialInfo.baseColorId);
    SamplerState baseColorSampler = Bindless(SamplerState, materialInfo.baseColorSampler);

    float4 sampleColor = baseColorTexture.Sample(baseColorSampler, input.uv) * materialInfo.baseColorFactor;
    clip(sampleColor.w - materialInfo.alphaCutOff);
#endif

    // Output id.
    outId = encodeTriangleIdInstanceId(primitiveId, input.id.x);
}

#endif