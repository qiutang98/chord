#include <asset/texture/texture.h>
#include <asset/serialize.h>

namespace chord
{
	using namespace graphics;

	const AssetTypeMeta TextureAsset::kAssetTypeMeta = createTextureTypeMeta();

	TextureAsset::TextureAsset(const AssetSaveInfo& saveInfo)
		: IAsset(saveInfo)
	{

	}

	void TextureAsset::onPostConstruct()
	{

	}

	bool TextureAsset::onSave()
	{
		std::shared_ptr<IAsset> asset = ptr<TextureAsset>();
		return saveAsset(asset, ECompressionMode::Lz4, m_saveInfo.path(), false);
	}

	void TextureAsset::onUnload()
	{

	}
}