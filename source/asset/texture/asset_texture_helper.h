#pragma once

#include <utils/utils.h>
#include <asset/asset_common.h>
#include <asset/asset.h>

namespace chord
{
	enum class ETextureFormat
	{
		R8G8B8A8,
		BC3,

		// Always mean RGB channel in GPU, don't care alpha.
		// But still load 4 channel from file.
		BC1,

		BC5,
		R8G8,

		// Load file with greyscale compute, and store in R8.
		Greyscale,
		R8, // Select R component.
		G8, // Select G component.
		B8, // Select B component.
		A8, // Select A component.

		BC4Greyscale, // Greyscale from file store within BC4.
		BC4R8, // Select R component.
		BC4G8, // Select G component.
		BC4B8, // Select B component.
		BC4A8, // Select A component.

		RGBA16Unorm,
		R16Unorm,

		MAX,
	};

	struct TextureAssetImportConfig : public IAssetImportConfig
	{
		// Texture is encoded in srgb color space?
		bool bSRGB = false;

		// Generate mipmap for this texture?
		bool bGenerateMipmap = false;

		// Alpha coverage mipmap cutoff.
		float alphaMipmapCutoff = 0.5f;

		// Texture format.
		ETextureFormat format;
	};
	using TextureAssetImportConfigRef = std::shared_ptr<TextureAssetImportConfig>;
}