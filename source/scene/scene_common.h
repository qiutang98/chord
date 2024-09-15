#pragma once

#include <asset/asset.h>
#include <application/application.h>
#include <shader/base.h>

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

    struct PerframeCollected
    {
        std::atomic_uint32_t gltfLod0MeshletCount  = 0;
        std::atomic_uint32_t gltfMeshletGroupCount = 0;
        std::vector<GPUObjectGLTFPrimitive> gltfPrimitives;

        DebugLineCtx* debugLineCtx = nullptr;
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

    class ISceneSystem
    {
    public:
        explicit ISceneSystem(const std::string& name, const std::string& decoratedName)
            : m_name(name), m_decoratedName(decoratedName)
        {

        }

        void drawUI(SceneRef scene);

    protected:
        virtual void onDrawUI(SceneRef scene) = 0;

    private:
        std::string m_name;
        std::string m_decoratedName;
    };
}