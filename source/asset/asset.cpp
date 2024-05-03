#include <asset/asset.h>
#include <project.h>
#include <asset/texture/texture.h>
#include <asset/serialize.h>
#include <asset/gltf/gltf.h>

namespace chord
{
	using namespace graphics;

	Events<IAsset, AssetRef> chord::onAssetMarkDirtyEvents;
	Events<IAsset, AssetRef> chord::onAssetSavedEvents;
	Events<IAsset, AssetRef> chord::onAssetNewlySaveToDiskEvents;

	Events<AssetManager, AssetRef> chord::onAssetRemoveEvents;
	Events<AssetManager, AssetRef> chord::onAssetInsertEvents;

	IAsset::IAsset(const AssetSaveInfo& saveInfo)
		: m_saveInfo(saveInfo)
	{

	}

	bool IAsset::save()
	{
		if (!isDirty())
		{
			return false;
		}

		if (m_saveInfo.empty())
		{
			LOG_ERROR("You must config save info before save an asset!");
			return false;
		}

		const bool bNewlySaveToDisk = !m_saveInfo.alreadyInDisk();

		bool bSaveResult = onSave();
		if (bSaveResult)
		{
			onAssetSavedEvents.broadcast(shared_from_this());

			if (bNewlySaveToDisk)
			{
				onAssetNewlySaveToDiskEvents.broadcast(shared_from_this());
			}
			m_bDirty = false;
		}

		return bSaveResult;
	}

	bool IAsset::markDirty()
	{
		if (m_bDirty)
		{
			return false;
		}

		m_bDirty = true;
		onAssetMarkDirtyEvents.broadcast(shared_from_this());

		return true;
	}

	std::filesystem::path IAsset::getSnapshotPath() const
	{
		std::u16string p = utf8::utf8to16(m_saveInfo.getSnapshotCachePath());
		std::filesystem::path cache = Project::get().getPath().cachePath.u16();

		return cache / p;
	}

	std::filesystem::path IAsset::getBinPath() const
	{
		std::u16string p = utf8::utf8to16(m_saveInfo.getBinCachePath());
		std::filesystem::path cache = Project::get().getPath().cachePath.u16();

		return cache / p;
	}

	AssetManager::AssetManager()
	{
		registerAsset(TextureAsset::kAssetTypeMeta);
		registerAsset(Scene::kAssetTypeMeta);
		registerAsset(GLTFAsset::kAssetTypeMeta);
	}

	void AssetManager::setupProject()
	{
		check(Project::get().isSetup());

		release();
		
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
			// All asset extension start with .asset
			if (extension.starts_with(".asset"))
			{
				// When setup project, don't care thread safe.
				tryLoadAsset(path, false);
			}
		}
	}

	AssetRef AssetManager::tryLoadAsset(const std::filesystem::path& savePath, bool bThreadSafe)
	{
		const auto saveInfo = AssetSaveInfo::buildRelativeAsset(savePath);
		check(saveInfo.alreadyInDisk());

		AssetRef result = nullptr;
		const uint64 hash = saveInfo.hash();
		{
			auto lockScope = bThreadSafe 
				? std::unique_lock<std::mutex>(m_assetsMapMutex) 
				: std::unique_lock<std::mutex>();

			if (m_assets[hash])
			{
				result = m_assets[hash];
			}
			else
			{
				// Load asset from disk.
				check(chord::loadAsset(result, savePath));

				insertAsset(hash, result);
			}
		}
		return result;
	}

	void AssetManager::registerAsset(const AssetTypeMeta& type)
	{
		check(m_registeredAssetType[type.name] == nullptr);
		m_registeredAssetType[type.name] = &type;
	}

	AssetRef AssetManager::removeAsset(uint64 id)
	{
		if (m_assets.contains(id))
		{
			auto asset = m_assets[id];
			onAssetRemoveEvents.broadcast(asset);

			auto savePath = asset->getSaveInfo().path();
			m_assets.erase(id);
			m_classifiedAssets[savePath.extension().string()].erase(id);

			return asset;
		}

		return nullptr;
	}

	void AssetManager::insertAsset(uint64 id, AssetRef asset)
	{
		auto savePath = asset->getSaveInfo().path();

		m_assets[id] = asset;
		m_classifiedAssets[savePath.extension().string()].insert(id);

		onAssetInsertEvents.broadcast(asset);
	}

	bool AssetManager::changeSaveInfo(const AssetSaveInfo& newInfo, AssetRef asset)
	{
		if (newInfo == asset->getSaveInfo())
		{
			return false;
		}

		check(m_assets.at(asset->getSaveInfo().hash()));
		check(!m_assets.contains(newInfo.hash()));

		removeAsset(asset->getSaveInfo().hash());

		asset->m_saveInfo = newInfo;
		insertAsset(newInfo.hash(), asset);

		asset->markDirty();
		return true;
	}

	void AssetManager::release()
	{
		// Clear all cache assets before setup project.
		m_assets.clear();
		m_classifiedAssets.clear();
	}
}