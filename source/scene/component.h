#pragma once

#include <scene/scene_common.h>

namespace chord
{
	class Component : NonCopyable
	{
		REGISTER_BODY_DECLARE();

	public:
		Component() = default;
		Component(SceneNodeRef sceneNode) : m_node(sceneNode) 
		{
		
		}

		virtual ~Component() = default;

		// ~Component Interface.
		virtual void init() { }
		virtual void tick(const ApplicationTickData& tickData) {}
		virtual void release() { }

		// Perview collect.
		virtual void onPerViewPerframeCollect(PerframeCollected& collector, const PerframeCameraView& cameraView) const { }
		// ~Component Interface.
		
		// Change owner node.
		bool setNode(SceneNodeRef node);

		// Get owner node.
		SceneNodeRef getNode() const;

		// Component is valid or not.
		bool isValid() const;

		// Mark dirty.
		void markDirty();

	protected:
		// Component host node.
		SceneNodeWeak m_node;
	};
}

