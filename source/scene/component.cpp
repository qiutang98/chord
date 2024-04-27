#include <scene/component.h>
#include <scene/scene.h>

namespace chord
{
	bool Component::setNode(SceneNodeRef node)
	{
		if (m_node.lock() != node)
		{
			m_node = node;

			markDirty();
			return true;
		}
		return false;
	}

	SceneNodeRef Component::getNode() const
	{
		return m_node.lock();
	}

	bool Component::isValid() const
	{
		return m_node.lock().get();
	}

	void Component::markDirty()
	{
		m_node.lock()->getScene()->markDirty();
	}
}