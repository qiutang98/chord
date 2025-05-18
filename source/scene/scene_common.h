#pragma once

#include <asset/asset.h>
#include <application/application.h>
#include <shader/base.h>
#include <utils/camera.h>

namespace chord
{
    class Scene;
    class SceneNode;
    class Component;
    struct DebugLineCtx;

    using SceneNodeRef  = std::shared_ptr<SceneNode>;
    using SceneNodeWeak = std::weak_ptr<SceneNode>;
    using ComponentRef  = std::shared_ptr<Component>;
    using ComponentWeak = std::weak_ptr<Component>;
    using SceneRef      = std::shared_ptr<Scene>;
    using SceneWeak     = std::weak_ptr<Scene>;

    class AccelerationStructureInstanceCollector
    {
    public:
        // Should TLAS fully rebuild or not.
        bool bTLASFullyRebuild = false;

        // Collected instances.
        std::vector<VkAccelerationStructureInstanceKHR> asInstances = {};

        bool isExistInstance() const
        {
            return !asInstances.empty();
        }
    };

    struct BuiltinMeshDrawInstance
    {
        graphics::BuiltinMesh* mesh;

        float3 color;

        float3 offset;
        float  scale;
    };


    struct PerframeCollected
    {
        std::atomic_uint32_t gltfLod0MeshletCount  = 0;
        std::atomic_uint32_t gltfMeshletGroupCount = 0;
        std::vector<GPUObjectGLTFPrimitive> gltfPrimitives;

        DebugLineCtx* debugLineCtx = nullptr;
        AccelerationStructureInstanceCollector asInstances;

        // Collect builtin mesh instances.
        std::vector<BuiltinMeshDrawInstance> builtinMeshInstances;

        // 
        void drawLowSphere(const ICamera* camera, const double3& position, float scale, const float3& color);
    };

    class UIComponentDrawDetails
    {
    public:
        std::string name;

        // This component is optional created in menu.
        bool bOptionalCreated;
        std::string decoratedName;

        std::function<bool(ComponentRef)> onDrawUI;
        std::function<std::shared_ptr<Component>()> factory;
    };




}