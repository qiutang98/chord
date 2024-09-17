#include "sky.h"
#include "sky.h"
#include <ui/ui_helper.h>
#include <scene/scene_node.h>
#include <scene/component/sky.h>

using namespace chord;

const UIComponentDrawDetails SkyComponent::kComponentUIDrawDetails = SkyComponent::createComponentUIDrawDetails();

UIComponentDrawDetails SkyComponent::createComponentUIDrawDetails()
{
	UIComponentDrawDetails result{};

	result.name = "Sky";
	result.bOptionalCreated = true;
	result.decoratedName = ICON_FA_SUN + std::string("  Sky");
	result.factory = []() { return std::make_shared<SkyComponent>(); };

	result.onDrawUI = [](ComponentRef component) -> bool
	{
		auto meshComponent = std::static_pointer_cast<SkyComponent>(component);
		bool bChanged = false;
		{

		}
		return bChanged;
	};

	return result;
}

constexpr float3 kDefaultSunDirection = math::vec3(0.1f, -0.8f, 0.2f);

SkyLightInfo chord::getDefaultSkyLightInfo()
{
	SkyLightInfo info { };

	info.direction = math::normalize(kDefaultSunDirection);

	return info;
}

SkyLightInfo SkyComponent::getSkyLightInfo() const
{
	SkyLightInfo info = getDefaultSkyLightInfo();

	info.direction = getSunDirection();
	return info;
}

float3 chord::SkyComponent::getSunDirection() const
{
	// Get scene node direction as sun direction.
	if (auto node = m_node.lock())
	{
		const auto& worldMatrix = node->getTransform()->getWorldMatrix();
		constexpr math::vec3 forward = math::vec3(0.0f, -1.0f, 0.0f);
		auto result = math::normalize(math::vec3(worldMatrix * math::vec4(forward, 0.0f)));
		if (result == forward)
		{
			result = math::normalize(math::vec3(1e-3f, -1.0f, 1e-3f));
		}


		return result;
	}

	return math::normalize(kDefaultSunDirection);
}
