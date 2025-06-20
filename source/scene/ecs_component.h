#pragma once

#include <scene/ecs.h>
#include <utils/utils.h>

namespace chord::ECS
{
	struct TreeHierarchy
	{
		EntityIndexType parentIndex = kRootEntityIndex;
		std::vector<EntityIndexType> childrenIndices;
	};

	struct Transform
	{
		math::dvec3 translation = { 0.0, 0.0, 0.0 };
		math::dvec3 rotation    = { 0.0, 0.0, 0.0 };
		math::dvec3 scale       = { 1.0, 1.0, 1.0 };
	};
}