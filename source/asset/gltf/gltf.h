#pragma once
#include <asset/asset.h>

namespace chord
{
	class GLTFAsset : public IAsset
	{
		REGISTER_BODY_DECLARE(IAsset);

		friend class AssetManager;
	public:
		static const AssetTypeMeta kAssetTypeMeta;

	private:
		static AssetTypeMeta createTypeMeta();
	};
}