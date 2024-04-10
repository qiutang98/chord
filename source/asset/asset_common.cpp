#include "asset_common.h"

namespace chord
{
	AssetRegistry& AssetRegistry::get()
	{
		static AssetRegistry registry;
		return registry;
	}

}

