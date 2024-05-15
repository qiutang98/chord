#include <asset/gltf/gltf.h>
#include <asset/gltf/gltf_helper.h>
#include <asset/serialize.h>

namespace chord
{
	using namespace graphics;

	const AssetTypeMeta GLTFAsset::kAssetTypeMeta = GLTFAsset::createTypeMeta();
	const AssetTypeMeta GLTFMaterialAsset::kAssetTypeMeta = GLTFMaterialAsset::createTypeMeta();

	GLTFAsset::GLTFAsset(const AssetSaveInfo& saveInfo)
		: IAsset(saveInfo)
	{
	}

	void GLTFAsset::onPostConstruct()
	{

	}

	bool GLTFAsset::onSave()
	{
		std::shared_ptr<IAsset> asset = ptr<GLTFAsset>();
		return saveAsset(asset, ECompressionMode::Lz4, m_saveInfo.path(), false);
	}

	void GLTFAsset::onUnload()
	{

	}

	GLTFMaterialAsset::GLTFMaterialAsset(const AssetSaveInfo& saveInfo)
		: IAsset(saveInfo)
	{
	}

	void GLTFMaterialAsset::onPostConstruct()
	{

	}

	bool GLTFMaterialAsset::onSave()
	{
		std::shared_ptr<IAsset> asset = ptr<GLTFMaterialAsset>();
		return saveAsset(asset, ECompressionMode::Lz4, m_saveInfo.path(), false);
	}

	void GLTFMaterialAsset::onUnload()
	{

	}
}
