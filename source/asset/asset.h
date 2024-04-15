#pragma once

#include <utils/utils.h>
#include <asset/asset_common.h>
#include <graphics/graphics.h>

namespace chord
{
	class IAsset : public std::enable_shared_from_this<IAsset>
	{
	public:
		explicit IAsset() = default;
		virtual ~IAsset() = default;

		// 
		explicit IAsset(const AssetSaveInfo& saveInfo);

	public:
		// ~IAsset virtual function.
		virtual IAssetType& getType() const = 0;
		virtual void onPostConstruct() = 0; // Call back when call AssetManager::createAsset
		virtual graphics::GPUTextureRef getSnapshotImage();
		virtual const std::string& getSuffix() const = 0; // Get suffix of asset.
		// ~IAsset virtual function.

	protected:
		// ~IAsset virtual function.
		virtual bool onSave() = 0;
		virtual void onUnload() = 0;
		// ~IAsset virtual function.

	public:
		// Get shared ptr.
		template<typename T> 
		std::shared_ptr<T> ptr()
		{
			static_assert(std::is_base_of_v<IAsset, T>, "T must derive from IAsset!");
			return std::static_pointer_cast<T>(IAsset::shared_from_this());
		}

		// Save asset.
		bool save();

		void unload();

	public:
		// Get asset store path.
		std::filesystem::path getStorePath() const 
		{
			return std::move(m_saveInfo.relativeAssetStorePath());
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

	private:
		// Asset is dirty or not.
		bool m_bDirty = false;

	protected:
		// Asset save info.
		AssetSaveInfo m_saveInfo { };

		// Raw asset save path relative to asset folder.
		u16str m_rawAssetPath {};

	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(m_saveInfo, m_rawAssetPath);
		}
	};

	class AssetManager : NonCopyable
	{
	public:
		void setupProject();

	private:
		void setupProjectRecursive(const std::filesystem::path& path);

		// Try load asset from path.
		std::shared_ptr<IAsset> tryLoadAsset(const std::filesystem::path& savePath);

		void insertAsset(const UUID& uuid, std::shared_ptr<AssetInterface> asset, bool bCareDirtyState);

	protected:
		// Map store all engine assetes.
		std::map<uint64, std::shared_ptr<IAsset>> m_assets;
	};
}