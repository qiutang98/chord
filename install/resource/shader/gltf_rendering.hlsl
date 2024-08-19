#include "gltf.h"

struct GLTFMeshDrawCmd
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
    uint objectId;
    uint meshletId;
    uint pipelineBin;
};

// Flag of switchFlags.
#define kLODEnableBit             0
#define kFrustumCullingEnableBit  1
#define kHZBCullingEnableBit      2
#define kMeshletConeCullEnableBit 3

struct GLTFDrawPushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(GLTFDrawPushConsts);

    uint cameraViewId;
    uint debugFlags;
    uint switchFlags;
    uint meshletCullGroupCountId;
    uint meshletCullGroupDetailId;
    uint meshletCullCmdId;

    uint hzb; // hzb texture id.  
    uint hzbMipCount;
    float uv2HzbX;
    float uv2HzbY;
    uint hzbMip0Width;
    uint hzbMip0Height;

    uint drawedMeshletCountId;
    uint drawedMeshletCmdId;
    uint drawedMeshletCountId_1;
    uint drawedMeshletCmdId_1;
    uint drawedMeshletCountId_2;
    uint drawedMeshletCmdId_2;

    uint targetAlphaMode; // 0 opaque, 1 masked, 2 blend.
    uint targetTwoSide; // 
};
CHORD_PUSHCONST(GLTFDrawPushConsts, pushConsts);

#ifndef __cplusplus // HLSL only area.

#include "bindless.hlsli"
#include "base.hlsli"
#include "debug.hlsli"
#include "debug_line.hlsli"

// GLTF material two state:
// 1. alphaMode: OPAQUE, MASK, BLEND.
// 2. bool doubleSided.
uint packPipelineState(uint bTwoSide, uint alphaMode)
{
    uint result = 0;
    result |= (alphaMode & 0x3) << 0; // Used two bit. 
    result |= (bTwoSide  & 0x1) << 2; // Used one bit. 

    // next
    return result;
}

uint isTwoSide(uint packData)
{
    return (packData >> 2) & 0x1;
}

uint getAlphaMode(uint packData)
{
    return (packData >> 0) & 0x3;
}

[numthreads(64, 1, 1)]
void perobjectCullingCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;  

    if (threadId >= scene.GLTFObjectCount)
    {
        return;
    } 

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, threadId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);

    const float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    const float4x4 localToClip = mul(perView.translatedWorldToClip, localToTranslatedWorld);

    const float3 posMin = primitiveInfo.posMin;
    const float3 posMax = primitiveInfo.posMax;
    const float3 posCenter = (posMin + posMax) * 0.5;
    const float3 extent = posMax - posCenter;

    // Frustum visible culling: use obb.
    if (shaderHasFlag(pushConsts.switchFlags, kFrustumCullingEnableBit))
    {
        if (frustumCulling(perView.frustumPlane, posCenter, extent, localToTranslatedWorld))
        {
            return;
        }
    }
    
    const uint dispatchCullGroupCount = (primitiveInfo.meshletCount + 63) / 64;
    const uint meshletBaseOffset = interlockedAddUint(pushConsts.meshletCullGroupCountId, dispatchCullGroupCount);

    RWByteAddressBuffer meshletCullGroupBuffer = RWByteAddressBindless(pushConsts.meshletCullGroupDetailId);
    for (uint i = 0; i < dispatchCullGroupCount; i ++)
    {
        const uint id = i + meshletBaseOffset;
        const uint meshletBaseIndex = primitiveInfo.meshletOffset + i * 64;

        // Fill meshlet dispatch param, pack to 64.
        uint2 result = uint2(threadId, meshletBaseIndex);
        meshletCullGroupBuffer.TypeStore(uint2, id, result);
    }
}

[numthreads(1, 1, 1)]
void fillMeshletCullCmdCS()
{
    const uint meshletGroupCount = BATL(uint, pushConsts.meshletCullGroupCountId, 0);
    uint4 cmdParameter;
    cmdParameter.x = meshletGroupCount;
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;

    BATS(uint4, pushConsts.meshletCullCmdId, 0, cmdParameter);
}

[numthreads(64, 1, 1)]
void meshletCullingCS(uint groupID : SV_GroupID, uint groupThreadID : SV_GroupThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint meshletGroupCount = BATL(uint, pushConsts.meshletCullGroupCountId, 0);
    const uint2 meshletGroup =  BATL(uint2, pushConsts.meshletCullGroupDetailId, groupID);

    uint objectId = meshletGroup.x;
    uint meshletId = groupThreadID + meshletGroup.y;

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;

    // Skip out of range meshlt. 
    if (meshletId >= primitiveInfo.meshletOffset + primitiveInfo.meshletCount)
    {
        return;
    }

    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    const float3 posMin    = meshlet.posMin;
    const float3 posMax    = meshlet.posMax;
    const float3 posCenter = 0.5 * (posMin + posMax);
    const float3 extent    = posMax - posCenter;
    
    float4x4 localToView = mul(perView.translatedWorldToView, localToTranslatedWorld);
    float4x4 localToClip = mul(perView.translatedWorldToClip, localToTranslatedWorld);
    if (shaderHasFlag(pushConsts.switchFlags, kLODEnableBit))
    {
        const float kErrorPixelThreshold = 1.0f; // One pixel error.

        const bool bFinalLod = (meshlet.parentError > 3e38f);
        const bool bFirstlOD = (meshlet.lod == 0);
        if (bFirstlOD && bFinalLod)
        {
            // Only exist one lod so always visible.
        }
        else
        {
            // Selected lod by error project. 
            float parentError = 0.0;
            if (!bFinalLod)
            {
                float4 sphere = transformSphere(float4(meshlet.parentPosCenter, meshlet.parentError), localToView, objectInfo.basicData.scaleExtractFromMatrix.w);
                parentError = projectSphereToScreen(sphere, perView.renderDimension.y, perView.camMiscInfo.x);
                if (parentError <= kErrorPixelThreshold) { return; }
            }

            if (!bFirstlOD)
            {
                float4 sphere = transformSphere(float4(meshlet.clusterPosCenter, meshlet.error), localToView, objectInfo.basicData.scaleExtractFromMatrix.w);
                float error = projectSphereToScreen(sphere, perView.renderDimension.y, perView.camMiscInfo.x);
                if (error > kErrorPixelThreshold) { return; }
            }

            // A terrible fact: sometimes projected parent error is smaller than current projected error. 
            //
        }
    }
    else
    {
        // Only draw lod #0 if no lod enable. 
        if (meshlet.lod != 0) { return; }
    }

    // Load material info.
    const GLTFMaterialGPUData materialInfo = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    // Do cone culling before frustum culling, it compute faster.
    if ((materialInfo.bTwoSided == 0) && shaderHasFlag(pushConsts.switchFlags, kMeshletConeCullEnableBit))
    {
    	float3 cameraPosLS = mul(objectInfo.basicData.translatedWorldToLocal, float4(0, 0, 0, 1)).xyz;
        if (dot(normalize(meshlet.coneApex - cameraPosLS), meshlet.coneAxis) >= meshlet.coneCutOff)
        {
            return;
        }
    }

    // Frustum visible culling: use obb.
    if (shaderHasFlag(pushConsts.switchFlags, kFrustumCullingEnableBit))
    {
        if (frustumCulling(perView.frustumPlane, posCenter, extent, localToTranslatedWorld))
        {
            return;
        }
    }

    uint drawCmdId = interlockedAddUint(pushConsts.drawedMeshletCountId);

    // Fill in draw cmd. 
    {
        GLTFMeshDrawCmd drawCmd;

        drawCmd.vertexCount    = unpackTriangleCount(meshlet.vertexTriangleCount) * 3;
        drawCmd.instanceCount  = 1;
        drawCmd.firstVertex    = 0;
        drawCmd.firstInstance  = drawCmdId;
        drawCmd.objectId       = objectId;
        drawCmd.meshletId      = meshletId;
        drawCmd.pipelineBin    = packPipelineState(materialInfo.bTwoSided, materialInfo.alphaMode);

        BATS(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, drawCmdId, drawCmd);
    }
}

[numthreads(1, 1, 1)]
void fillHZBCullParamCS()
{
    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);
    uint4 cmdParameter;
    cmdParameter.x = (meshletCount + 63) / 64;
    cmdParameter.y = 1;
    cmdParameter.z = 1;
    cmdParameter.w = 1;

    BATS(uint4, pushConsts.meshletCullCmdId, 0, cmdParameter);
}

[numthreads(64, 1, 1)]
void HZBCullingCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);
    if (threadId >= meshletCount)
    {
        return;
    }

    const GLTFMeshDrawCmd drawCmd = BATL(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, threadId);
    uint objectId  = drawCmd.objectId;
    uint meshletId = drawCmd.meshletId;

    //
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);

    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    float4x4 localToTranslatedWorld = objectInfo.basicData.localToTranslatedWorld;
    const float3 posMin = meshlet.posMin;
    const float3 posMax = meshlet.posMax;
    const float3 posCenter = (posMin + posMax) * 0.5;
    const float3 extent = posMax - posCenter;

    // Project to prev frame and do some occlusion culling. 
    bool bVisible = true;
    if (shaderHasFlag(pushConsts.switchFlags, kHZBCullingEnableBit))
    {
        float3 maxUVz = -10.0;
        float3 minUVz =  10.0;

        const float4x4 mvp = 
        #if DIM_HZB_CULLING_PHASE_0
            mul(perView.translatedWorldToClipLastFrame, objectInfo.basicData.localToTranslatedWorldLastFrame);
        #else
            mul(perView.translatedWorldToClip, localToTranslatedWorld);
        #endif

        for (uint i = 0; i < 8; i ++)
        {
            const float3 extentPos = posCenter + extent * kExtentApplyFactor[i];
            const float3 UVz = projectPosToUVz(extentPos, mvp);
            minUVz = min(minUVz, UVz);
            maxUVz = max(maxUVz, UVz);
        }

        if (maxUVz.z < 1.0f && minUVz.z > 0.0f)
        {
            // Clamp min&max uv.
            minUVz.xy = saturate(minUVz.xy);
            maxUVz.xy = saturate(maxUVz.xy);

            // UV convert to hzb space.
            maxUVz.x *= pushConsts.uv2HzbX;
            maxUVz.y *= pushConsts.uv2HzbY;
            minUVz.x *= pushConsts.uv2HzbX;
            minUVz.y *= pushConsts.uv2HzbY;

            const float2 bounds = (maxUVz.xy - minUVz.xy) * float2(pushConsts.hzbMip0Width, pushConsts.hzbMip0Height);
            if (any(bounds <= 0))
            {
                // Zero area just culled.
                bVisible = false;
            }
            else
            {
                // Use max bounds dim as sample mip level.
                const uint mipLevel = uint(min(ceil(log2(max(1.0, max2(bounds)))), pushConsts.hzbMipCount - 1));
                const uint2 mipSize = uint2(pushConsts.hzbMip0Width, pushConsts.hzbMip0Height) / uint(exp2(mipLevel));

                // Load hzb texture.
                Texture2D<float> hzbTexture = TBindless(Texture2D, float, pushConsts.hzb);

                const uint2 minPos = uint2(minUVz.xy * mipSize);
                const uint2 maxPos = uint2(maxUVz.xy * mipSize);

                float zMin = hzbTexture.Load(int3(minPos, mipLevel));
                                            zMin = min(zMin, hzbTexture.Load(int3(maxPos, mipLevel)));
                if (maxPos.x != minPos.x) { zMin = min(zMin, hzbTexture.Load(int3(maxPos.x, minPos.y, mipLevel))); }
                if (maxPos.y != minPos.y) { zMin = min(zMin, hzbTexture.Load(int3(minPos.x, maxPos.y, mipLevel))); }
                if (zMin > maxUVz.z)
                {
                    // Occluded by hzb.
                    bVisible = false;
                }
            }
        }
    }

    // Load material info.
    GLTFMeshDrawCmd visibleDrawCmd = drawCmd;

    // If visible, add to draw list.
    if (bVisible)
    {
        uint drawCmdId = interlockedAddUint(pushConsts.drawedMeshletCountId_1);

        visibleDrawCmd.firstInstance  = drawCmdId;
        BATS(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId_1, drawCmdId, visibleDrawCmd);

    #if DIM_PRINT_DEBUG_BOX
        uint packColor = simpleHashColorPack(meshletId);
        LineDrawVertex worldPosExtents[8];
        for (uint i = 0; i < 8; i ++)
        {
            const float3 extentPos = posCenter + extent * kExtentApplyFactor[i];
            const float3 extentPosRS = mul(localToTranslatedWorld, float4(extentPos, 1.0)).xyz;
            worldPosExtents[i].translatedWorldPos = extentPosRS;
            worldPosExtents[i].color = packColor;
        }
        addBox(scene, worldPosExtents);
    #endif
    }
    #if DIM_HZB_CULLING_PHASE_0
    else
    {
        uint drawCmdId = interlockedAddUint(pushConsts.drawedMeshletCountId_2);

        visibleDrawCmd.firstInstance  = drawCmdId;
        BATS(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId_2, drawCmdId, visibleDrawCmd);
    }
    #endif
}

// Loop all visibile meshlet, filter mesh and fill all draw cmd.
[numthreads(64, 1, 1)]
void fillPipelineDrawParamCS(uint threadId : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint meshletCount = BATL(uint, pushConsts.drawedMeshletCountId, 0);
    if (threadId >= meshletCount)
    {
        return;
    }

    const GLTFMeshDrawCmd drawCmd = BATL(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, threadId);
    uint objectId  = drawCmd.objectId;
    uint meshletId = drawCmd.meshletId;

    //
    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFMaterialGPUData materialInfo = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

    const uint bTwoSided = materialInfo.bTwoSided;
    const uint alphaMode = materialInfo.alphaMode;
    check(bTwoSided == 0 || bTwoSided == 1);
    check(alphaMode == 0 || alphaMode == 1 || alphaMode == 2);

    const bool bAlphaPass = (pushConsts.targetAlphaMode == alphaMode);
    const bool bTwoSidedPass = (pushConsts.targetTwoSide == bTwoSided);

    // Visible, fill draw command.
    if (bAlphaPass && bTwoSidedPass)
    {
        // Load material info.
        GLTFMeshDrawCmd visibleDrawCmd = drawCmd;

        uint drawCmdId = interlockedAddUint(pushConsts.drawedMeshletCountId_1);
        visibleDrawCmd.firstInstance  = drawCmdId;
        BATS(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId_1, drawCmdId, visibleDrawCmd);
    }
}

struct VisibilityPassVS2PS
{
    float4 positionHS : SV_Position;
    nointerpolation uint2 id : TEXCOORD0;
#if DIM_MASKED_MATERIAL
    float2 uv : TEXCOORD1;
#endif
};
 
void visibilityPassVS(
    uint vertexId : SV_VertexID, 
    uint instanceId : SV_INSTANCEID,
    out VisibilityPassVS2PS output)
{
    const GLTFMeshDrawCmd drawCmd = BATL(GLTFMeshDrawCmd, pushConsts.drawedMeshletCmdId, instanceId);
    PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    uint objectId  = drawCmd.objectId;
    uint meshletId = drawCmd.meshletId;

    const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
    const GLTFPrimitiveBuffer primitiveInfo = BATL(GLTFPrimitiveBuffer, scene.GLTFPrimitiveDetailBuffer, objectInfo.GLTFPrimitiveDetail);

    const GLTFPrimitiveDatasBuffer primitiveDataInfo = BATL(GLTFPrimitiveDatasBuffer, scene.GLTFPrimitiveDataBuffer, primitiveInfo.primitiveDatasBufferId);
    const GPUGLTFMeshlet meshlet = BATL(GPUGLTFMeshlet, primitiveDataInfo.meshletBuffer, meshletId);

    ByteAddressBuffer positionDataBuffer = ByteAddressBindless(primitiveDataInfo.positionBuffer);

    uint verticesCount = unpackVertexCount(meshlet.vertexTriangleCount);

    ByteAddressBuffer meshletDataBuffer = ByteAddressBindless(primitiveDataInfo.meshletDataBuffer);

    const uint triangleId = vertexId / 3;

    uint triangleIndicesSampleOffset = (meshlet.dataOffset + verticesCount + triangleId) * 4;
    uint indexTri = meshletDataBuffer.Load(triangleIndicesSampleOffset);

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

    output.id.x = encodeShadingMeshlet((uint)(EShadingType::GLTF_MetallicRoughnessPBR), meshletId);
    output.id.y = encodeTriangleIdObjectId(triangleId, objectId);
} 

void visibilityPassPS(in VisibilityPassVS2PS input, out uint2 outId : SV_Target0)
{
#if DIM_MASKED_MATERIAL
    uint triangleId;
    uint objectId;
    decodeTriangleIdObjectId(input.id.y, triangleId, objectId);

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
    outId = input.id;
}

#endif // !__cplusplus