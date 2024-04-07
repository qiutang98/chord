#pragma once

#include <utils/utils.h>

namespace chord
{
	// Asset type meta info, register in runtime.
	class IAssetType
	{
	public:
		const std::string name;
		const std::string icon;
		const std::string decoratedName;


	};
}