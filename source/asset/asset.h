#pragma once

#include <utils/utils.h>
#include <asset/asset_common.h>
#include <graphics/graphics.h>
#include <utils/thread.h>
#include <graphics/resource.h>
#include <graphics/uploader.h>

namespace chord
{
	class IAsset;

	template<typename T>
	void checkAssetDerivedType()
	{
		if constexpr (!std::is_same_v<T, IAsset>)
		{
			static_assert(std::is_constructible_v<T, const AssetSaveInfo&>);
			static_assert(std::is_base_of_v<IAsset, T>);

			T::kAssetTypeMeta; // All asset exist meta data.
		}
	}

	// We always store asset meta data, bin data only load when required.
	class IAsset : public std::enable_shared_from_this<IAsset>
	{
		REGISTER_BODY_DECLARE();
		friend class AssetManager;

	public:
		IAsset() = default;
		virtual ~IAsset() = default;

		// 
		explicit IAsset(const AssetSaveInfo& saveInfo);

	protected:
		// ~IAsset virtual function.
		virtual bool onSave() = 0;
		virtual void onUnload() = 0;
		// ~IAsset virtual function.

	public:
		graphics::GPUTextureAssetRef getSnapshotImage();

		// Save snapshot in disk, will change snapshot dimension.
		bool saveSnapShot(const math::uvec2& snapshot, const std::vector<uint8>& datas);

	public:
		// Get shared ptr.
		template<typename T> 
		std::shared_ptr<T> ptr()
		{
			checkAssetDerivedType<T>();
			return std::static_pointer_cast<T>(IAsset::shared_from_this());
		}

		// Save asset.
		bool save();

	protected:
		// ~IAsset virtual function.

		// Call back when call AssetManager::createAsset
		virtual void onPostConstruct() = 0; 
		// ~IAsset virtual function.

	public:
		// Get asset store path.
		std::filesystem::path getStorePath() const 
		{
			return m_saveInfo.relativeAssetStorePath();
		}

		const AssetSaveInfo& getSaveInfo() const
		{ 
			return m_saveInfo; 
		}

		// Get asset store name.
		const auto& getName() const 
		{ 
			return m_saveInfo.getName();
		}

		// Get asset store folder.
		const auto& getStoreFolder() const 
		{ 
			return m_saveInfo.getStoreFolder(); 
		}

		// 
		bool isSaveInfoEmpty() const 
		{ 
			return m_saveInfo.empty(); 
		}

		// Asset is dirty or not.
		bool isDirty() const 
		{ 
			return m_bDirty; 
		}

		// Mark asset is dirty.
		bool markDirty();

		std::filesystem::path getSnapshotPath() const;
		std::filesystem::path getBinPath() const;

	private:
		// Asset is dirty or not.
		bool m_bDirty = false;

		// Snapshot texture weak object pointer.
		graphics::GPUTextureAssetWeak m_snapshotWeakPtr { };

	protected:
		// Asset save info.
		AssetSaveInfo m_saveInfo { };

		// Raw asset save path relative to asset folder.
		u16str m_rawAssetPath {};

		// Snapshot dimension.
		math::uvec2 m_snapshotDimension { 0, 0 };
	};
	using AssetRef = std::shared_ptr<IAsset>;

	// Global events when asset mark dirty.
	extern ChordEvent<AssetRef> onAssetMarkDirtyEvents;

	// Global events when asset saved.
	extern ChordEvent<AssetRef> onAssetSavedEvents;

	// Global events when asset newly save to disk.
	extern ChordEvent<AssetRef> onAssetNewlySaveToDiskEvents;

	// Engine asset manager.
	class AssetManager : NonCopyable
	{
	public:
		explicit AssetManager();

		void setupProject();

	private:
		void setupProjectRecursive(const std::filesystem::path& path);

		// Try load asset from path which store in disk.
		AssetRef tryLoadAsset(const std::filesystem::path& savePath, bool bThreadSafe);

		// Register asset type.
		void registerAsset(const AssetTypeMeta& type);

		// Removae asset from map.
		AssetRef removeAsset(uint64 id);

		// Insert asset to map.
		void insertAsset(uint64 id, AssetRef asset);

	public:
		template<typename T>
		std::shared_ptr<T> getOrLoadAsset(const std::filesystem::path& savePath, bool bThreadSafe)
		{
			checkAssetDerivedType<T>();
			return std::dynamic_pointer_cast<T>(tryLoadAsset(savePath, bThreadSafe));
		}

		template<typename T>
		std::shared_ptr<T> createAsset(const AssetSaveInfo& saveInfo, bool bThreadSafe)
		{
			if (!bThreadSafe)
			{
				check(isInMainThread());
			}

			checkAssetDerivedType<T>();
			const uint64 hash = saveInfo.hash();

			std::shared_ptr<T> newAsset = nullptr;
			{
				auto lockScope = bThreadSafe
					? std::unique_lock<std::mutex>(m_assetsMapMutex)
					: std::unique_lock<std::mutex>();

				check(!m_assets[hash] && "Don't create asset with same save info.");
				newAsset = std::make_shared<T>(saveInfo);

				// Call post construct function.
				newAsset->onPostConstruct();

				// Call insert asset first.
				insertAsset(hash, newAsset);
			}

			// Return result.
			return newAsset;
		}

		template<typename T>
		void unload(std::shared_ptr<T> asset, bool bThreadSafe)
		{
			checkAssetDerivedType<T>();
			const uint64 hash = asset->getSaveInfo().hash();

			auto lockScope = bThreadSafe
				? std::unique_lock<std::mutex>(m_assetsMapMutex)
				: std::unique_lock<std::mutex>();

			// Unload all data of asset.
			asset->onUnload();

			// Remove from map.
			removeAsset(hash);
		}

		// Get all exist asset type map.
		const auto& getRegisteredAssetMap() const
		{
			return m_registeredAssetType;
		}

		AssetRef at(uint64 id) const
		{
			return m_assets.at(id);
		}

		bool changeSaveInfo(const AssetSaveInfo& newInfo, AssetRef asset);

		void release();

	protected:
		// Static const registered meta infos.
		std::unordered_map<std::string, const AssetTypeMeta*> m_registeredAssetType;

		// Map store all engine assetes.
		mutable std::mutex m_assetsMapMutex;

		// All meta asset cache.
		std::map<uint64, AssetRef> m_assets;

		// Classify by save info extension.
		std::map<std::string, std::set<uint64>> m_classifiedAssets; 
	};

	extern ChordEvent<AssetRef> onAssetRemoveEvents;
	extern ChordEvent<AssetRef> onAssetInsertEvents;
}

