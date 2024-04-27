#pragma once

#include <asset/asset.h>
#include <application/application.h>

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

    class UIComponentDrawDetails
    {
    public:
        // This component is optional created in menu.
        bool bOptionalCreated;
        std::string icon;

        std::function<bool(ComponentRef)> onDrawUI;
    };
}