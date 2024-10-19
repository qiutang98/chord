// Visualize pass for accelerate structure. 

#include "base.h"

struct AccelerationStructureVisualizePushConsts
{
    CHORD_DEFAULT_COMPARE_ARCHIVE(AccelerationStructureVisualizePushConsts);

    uint cameraViewId;
    uint uav;

};
CHORD_PUSHCONST(AccelerationStructureVisualizePushConsts, pushConsts);

#ifndef __cplusplus

#include "base.hlsli"
#include "bindless.hlsli"
#include "gltf.h"

[[vk::binding(0, 1)]] RaytracingAccelerationStructure topLevelAS;



[numthreads(8, 8, 1)]
void mainCS(uint2 workPos : SV_DispatchThreadID)
{
    const PerframeCameraView perView = LoadCameraView(pushConsts.cameraViewId);
    const GPUBasicData scene = perView.basicData;

    const uint2 renderDim = uint2(perView.renderDimension.xy);
    if (any(workPos >= renderDim))
    {
        return;
    }

    const float2 fragCoord = workPos + 0.5;
    const float2 uv = fragCoord * perView.renderDimension.zw;

    float3 worldDirectionRS;
    {
        const float4 viewPointCS = float4(screenUvToNdcUv(uv), kFarPlaneZ, 1.0);
        float4 viewPointRS = mul(perView.clipToTranslatedWorld, viewPointCS);
        worldDirectionRS = normalize(viewPointRS.xyz / viewPointRS.w);
    }

    float3 resultColor = 0.0;

    GIRayQuery query;
    RayDesc ray = getRayDesc(0, worldDirectionRS, 0.0, kDefaultRayQueryTMax);

    // 
    const uint traceFlag = RAY_FLAG_NONE;

    query.TraceRayInline(topLevelAS, traceFlag, 0xFF, ray);
    query.Proceed();
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
    #if 0
        query.CommittedInstanceIndex();
        query.CommittedPrimitiveIndex();
        query.CommittedGeometryIndex();
        float t = query.CommittedRayT();
        query.CommittedTriangleBarycentrics();
        query.CommittedTriangleFrontFace();
    #endif

        const uint objectId = query.CommittedInstanceID();
        const GPUObjectGLTFPrimitive objectInfo = BATL(GPUObjectGLTFPrimitive, scene.GLTFObjectBuffer, objectId);
        const GLTFMaterialGPUData materialInfo  = BATL(GLTFMaterialGPUData, scene.GLTFMaterialBuffer, objectInfo.GLTFMaterialData);

        float2 hitTriangleBarycentrics = query.CommittedTriangleBarycentrics();

        resultColor.xyz = simpleHashColor(objectId) * hitTriangleBarycentrics.xyy;
    }

    storeRWTexture2D_float3(pushConsts.uav, workPos, resultColor);
}

#endif 