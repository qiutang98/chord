#include <scene/scene_node.h>
#include <scene/scene.h>

namespace chord
{
    SceneNode::~SceneNode()
    {
        LOG_TRACE("SceneNode {0} with GUID {1} destroy.", m_name.u8(), m_id);
    }

    SceneNodeRef SceneNode::create(const size_t id, const u16str& name, SceneRef scene)
    {
        auto res = std::make_shared<SceneNode>();

        res->m_id = id;
        res->m_name = name;

        res->setComponent(std::make_shared<Transform>(res));
        res->m_scene = scene;

        LOG_TRACE("SceneNode {0} with GUID {1} construct.", res->m_name.u8(), res->m_id);
        return res;
    }

    void SceneNode::tick(const ApplicationTickData& tickData)
    {
        for (auto& comp : m_components)
        {
            comp.second->tick(tickData);
        }
    }

    void SceneNode::perframeCollect(PerframeCollected& collector)
    {
        for (auto& comp : m_components)
        {
            comp.second->onPerframeCollect(collector);
        }
    }

    GPUObjectBasicData SceneNode::getObjectBasicData() const
    {
        GPUObjectBasicData data{};

        data.localToWorld = getTransform()->getWorldMatrix();

        return GPUObjectBasicData();
    }

    void SceneNode::removeComponent(const std::string& id)
    {
        m_components.erase(id);
        markDirty();
    }

    void SceneNode::markDirty()
    {
        m_scene.lock()->markDirty();
    }

    bool SceneNode::canSetNewVisibility()
    {
        // Parent is visible, or parent is unvisible but current visible is true;
        return getParent()->getVisibility() || m_bVisibility;
    }

    bool SceneNode::canSetNewStatic()
    {
        // Parent is static, or parent is un static but current static is false;
        return getParent()->getStatic() || m_bStatic;
    }

    void SceneNode::setVisibilityImpl(bool bState, bool bForce)
    {
        if (m_bVisibility != bState)
        {
            if (!bForce)
            {
                // Parent is unvisible, but new state is visible. stop set.
                if (!canSetNewVisibility())
                {
                    return;
                }
            }

            m_bVisibility = bState;
            for (auto& child : m_children)
            {
                child->setVisibilityImpl(bState, true);
            }

            markDirty();
        }
    }

    void SceneNode::setStaticImpl(bool bState, bool bForce)
    {
        if (m_bStatic != bState)
        {
            if (!bForce)
            {
                // New state is static, but parent is no static, stop set.
                if (!canSetNewStatic())
                {
                    return;
                }
            }

            m_bStatic = bState;
            for (auto& child : m_children)
            {
                child->setStaticImpl(bState, true);
            }

            markDirty();
        }
    }

    const bool SceneNode::isRoot() const
    {
        return m_id == Scene::kRootId;
    }

    std::shared_ptr<Component> SceneNode::getComponent(const std::string& id)
    {
        if (m_components.contains(id))
        {
            return m_components[id];
        }
        else
        {
            return nullptr;
        }
    }

    bool SceneNode::hasComponent(const std::string& id) const
    {
        return m_components.count(id) > 0;
    }

    bool SceneNode::setName(const u16str& in)
    {
        if (in != m_name)
        {
            m_name = in;
            m_scene.lock()->markDirty();
            return true;
        }
        return false;
    }

    // node is son of this node?
    bool SceneNode::isSon(SceneNodeRef node) const
    {
        if (node->isRoot())
        {
            return false;
        }

        if (isRoot())
        {
            return true;
        }

        SceneNodeRef nodeP = node->m_parent.lock();
        while (nodeP)
        {
            if (nodeP->getId() == m_id)
            {
                return true;
            }
            nodeP = nodeP->m_parent.lock();
        }
        return false;
    }

    // node is the direct son of this node.
    bool SceneNode::isSonDirectly(SceneNodeRef node) const
    {
        if (node->isRoot())
        {
            return false;
        }

        SceneNodeRef nodeP = node->m_parent.lock();
        return nodeP->getId() == m_id;
    }

    void SceneNode::addChild(SceneNodeRef child)
    {
        m_children.push_back(child);
        markDirty();
    }

    // Set p as this node's new parent.
    void SceneNode::setParent(SceneNodeRef p)
    {
        if (isRoot())
        {
            return;
        }

        // remove old parent's referece if exist.
        if (auto oldP = m_parent.lock())
        {
            // Just return if parent same.
            if (oldP->getId() == p->getId())
            {
                return;
            }

            oldP->removeChild(getId());
        }

        // prepare new reference.
        m_parent = p;

        if (!p->isRoot())
        {
            setVisibility(p->getVisibility());
            setStatic(p->getStatic());
        }
        p->addChild(shared_from_this());

        // Only update this node depth.
        getTransform()->invalidateWorldMatrix();

        markDirty();
    }

    void SceneNode::removeChild(SceneNodeRef o)
    {
        removeChild(o->getId());
    }

    void SceneNode::removeChild(size_t inId)
    {
        size_t id = 0;
        while (m_children[id]->getId() != inId)
        {
            id++;
        }

        // swap to the end and pop.
        if (id < m_children.size())
        {
            std::swap(m_children[id], m_children[m_children.size() - 1]);
            m_children.pop_back();
        }

        markDirty();
    }


    bool SceneNode::unparent()
    {
        if (auto parent = m_parent.lock())
        {
            markDirty();

            parent->removeChild(getId());
            m_parent = {};

            return true;
        }

        return false;
    }
}