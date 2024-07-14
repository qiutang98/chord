#pragma once

#include <scene/scene_common.h>
#include <scene/component/transform.h>

namespace chord
{
    class SceneNode : public std::enable_shared_from_this<SceneNode>
    {
        REGISTER_BODY_DECLARE();
        friend Scene;

    public:
        // Just provide for cereal, don't use it runtime.
        SceneNode() = default;
        virtual ~SceneNode();

        static SceneNodeRef create(const size_t id, const u16str& name, SceneRef scene);
        void tick(const ApplicationTickData& tickData);
        void perframeCollect(PerframeCollected& collector);

        GPUObjectBasicData getObjectBasicData() const;

    public:
        const auto& getId() const { return m_id; }
        const auto& getName() const { return m_name; }
        const auto& getChildren() const { return m_children; }
        auto& getChildren() { return m_children; }
        const bool isRoot() const;

        bool getVisibility() const { return m_bVisibility; }
        bool getStatic() const { return m_bStatic; }

        std::shared_ptr<Component> getComponent(const std::string& id);

        template <typename T>
        std::shared_ptr<T> getComponent()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
            return std::dynamic_pointer_cast<T>(getComponent(typeid(T).name()));
        }

        auto getTransform() { return getComponent<Transform>(); }
        auto getParent() { return m_parent.lock(); }
        auto getPtr() { return shared_from_this(); }
        auto getScene() { return m_scene.lock(); }

        bool hasComponent(const std::string& id) const;

        template <class T>
        bool hasComponent() const
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
            return hasComponent(typeid(T).name());
        }

        bool setName(const u16str& in);

        bool isSon(SceneNodeRef p) const; // p is the son of this node.
        bool isSonDirectly(SceneNodeRef p) const; // p is the direct son of this node.

        void markDirty();

        bool canSetNewVisibility();
        bool canSetNewStatic();

        void setVisibility(bool bState) { setVisibilityImpl(bState, false); }
        void setStatic(bool bState) { setStaticImpl(bState, false); }

    private:
        void addChild(SceneNodeRef child);

        void removeChild(SceneNodeRef o);
        void removeChild(size_t id);

        // set p as this node's new parent.
        void setParent(SceneNodeRef p);

        template<typename T>
        void setComponent(std::shared_ptr<T> component);

        void setComponent(const std::string& type, std::shared_ptr<Component> component);

        // Remove parent relationship.
        bool unparent();

        // remove component.
        void removeComponent(const std::string& id);

        // Set node view state.
        void setVisibilityImpl(bool bState, bool bForce);

        // Set node static state.
        void setStaticImpl(bool bState, bool bForce);

    private:
        // This node visibility state.
        bool m_bVisibility = true;

        // This node static state.
        bool m_bStatic = true;

        // Id of scene node.
        size_t m_id;

        // Inspector name, utf8 encode.
        u16str m_name = u16str("untitled");

        // Reference of parent.
        SceneNodeWeak m_parent;

        // Reference of scene.
        SceneWeak m_scene;

        // Owner of components.
        std::map<std::string, ComponentRef> m_components;

        // Owner of children.
        std::vector<SceneNodeRef> m_children;
    };

    template<typename T>
    inline void SceneNode::setComponent(std::shared_ptr<T> component)
    {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");

        component->setNode(getPtr());
        std::string type = typeid(T).name();

        auto it = m_components.find(type);
        if (it != m_components.end())
        {
            it->second = component;
        }
        else
        {
            m_components.insert(std::make_pair(type, component));
        }
    }

    inline void SceneNode::setComponent(
        const std::string& type, 
        std::shared_ptr<Component> component)
    {
        component->setNode(getPtr());

        auto it = m_components.find(type);
        if (it != m_components.end())
        {
            it->second = component;
        }
        else
        {
            m_components.insert(std::make_pair(type, component));
        }
    }
}