#include <asset/asset.h>
#include <project.h>

namespace chord
{
	using namespace graphics;

	IAsset::IAsset(const AssetSaveInfo& saveInfo)
		: m_saveInfo(saveInfo)
	{

	}

	GPUTextureRef IAsset::getSnapshotImage()
	{
		return getContext().getBuiltinTextures().white;
	}


	void AssetManager::setupProject()
	{
		check(Project::get().isSetup());

		// Clear all cache assets before setup project.
		m_assets.clear();
		
		// Build from asset path recursive.
		setupProjectRecursive(Project::get().getPath().assetPath.u16());
	}

	void AssetManager::setupProjectRecursive(const std::filesystem::path& path)
	{
		const bool bFolder = std::filesystem::is_directory(path);
		if (bFolder)
		{
			for (const auto& entry : std::filesystem::directory_iterator(path))
			{
				setupProjectRecursive(entry);
			}
		}
		else
		{
			const auto extension = path.extension().string();
			if (extension.starts_with(".dark"))
			{
				tryLoadAsset(path);
			}
		}
	}

}