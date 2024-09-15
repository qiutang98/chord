#pragma once

#include <scene/component.h>
#include <shader/base.h>

namespace chord
{
	extern SkyLightInfo getDefaultSkyLightInfo();

	class SkyComponent : public Component
	{
		REGISTER_BODY_DECLARE(Component);
		using Super = Component;

	public:
		static const UIComponentDrawDetails kComponentUIDrawDetails;

		SkyComponent() = default;
		SkyComponent(SceneNodeRef sceneNode) : Component(sceneNode) { }

		virtual ~SkyComponent() = default;

		SkyLightInfo getSkyLightInfo() const;
		float3 getSunDirection() const;

	private:
		static UIComponentDrawDetails createComponentUIDrawDetails();

	private:

	};
}