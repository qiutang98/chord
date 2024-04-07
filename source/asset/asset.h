#pragma once

#include <utils/utils.h>

namespace chord
{
	enum class EAssetType
	{
		Texture,
		Material,
		GLTF,
		Scene,

		MAX
	};

	class IAsset : public std::enable_shared_from_this<IAsset>
	{
		REGISTER_BODY_DECLARE();
	public:
		IAsset() = default;


	};
}