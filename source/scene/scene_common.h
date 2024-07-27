#pragma once

#include <asset/asset.h>
#include <application/application.h>
#include <shader/base.h>

namespace chord
{
    class Scene;
    class SceneNode;
    class Component;

    using SceneNodeRef  = std::shared_ptr<SceneNode>;
    using SceneNodeWeak = std::weak_ptr<SceneNode>;
    using ComponentRef  = std::shared_ptr<Component>;
    using ComponentWeak = std::weak_ptr<Component>;
    using SceneRef      = std::shared_ptr<Scene>;
    using SceneWeak     = std::weak_ptr<Scene>;

    struct PerframeCollected
    {
        std::atomic_uint32_t gltfLod0MeshletCount = 0;
        std::vector<GPUObjectGLTFPrimitive> gltfPrimitives;
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