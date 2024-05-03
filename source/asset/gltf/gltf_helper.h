#pragma once

#include <asset/asset.h>
#include <asset/asset_common.h>

namespace chord
{
	struct GLTFAssetImportConfig : public IAssetImportConfig
	{
		
	};
	using GLTFAssetImportConfigRef = std::shared_ptr<GLTFAssetImportConfig>;
}