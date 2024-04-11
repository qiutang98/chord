#pragma once

#include <utils/utils.h>

namespace chord
{
	// There are three type asset.
	//	#0. Temp: newly created, no save in disk yet.
	//  #1. Builtin: engine builtin asset, also is temp asset.
	//  #2. Normal: project asset already in disk.
	class AssetSaveInfo
	{
	public:
		static const char* kTempFolderStartChar;
		static const char* kBuiltinFileStartChar;

		AssetSaveInfo() = default;
		explicit AssetSaveInfo(const u16str& name, const u16str& storeFolder);

		static AssetSaveInfo buildTemp(const u16str& name);
		static AssetSaveInfo buildBuiltin(const std::string& name);
		static AssetSaveInfo buildRelativeAsset(const std::filesystem::path& savePath);

		// Default compare.
		bool operator==(const AssetSaveInfo&) const = default;

	public:
		// Temp asset folder start with "*".
		bool isTemp() const;

		// Builtin asset folder start with "*" and file name start with "~".
		bool isBuiltin() const;

		// This save info path already in disk.
		bool alreadyInDisk() const;

		// Get store path in disk.
		const std::filesystem::path path() const;

		// Get store path relative to project asset folder.
		const std::filesystem::path relativeAssetStorePath() const;

		const auto& getName() const
		{
			return m_name;
		}

		const auto& getStoreFolder() const
		{
			return m_storeFolder;
		}

		bool setName(const u16str& newValue);
		bool setStoreFolder(const u16str& newValue);

		bool empty() const
		{
			return m_name.empty();
		}

		// Snapshot file cache path.
		std::string getSnapshotCachePath() const;

		// Bin file cache path.
		std::string getBinCachePath() const;

	private:
		u16str m_name; // Asset name only, std::filesystem::path.filename.
		u16str m_storeFolder; // Asset store folder relative to project asset folder.

	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(m_name, m_storeFolder);
		}
	};


	class AssetImportConfig
	{
	public:
		bool bImportable;
		std::string rawDataExtension;
	};

	// Asset type meta info, register in runtime.
	class IAssetType
	{
	public:
		std::string name;
		std::string icon;
		std::string decoratedName;

		// Asset import config.
		std::unique_ptr<AssetImportConfig> importConfig;
	};

	class AssetRegistry
	{
	public:
		static AssetRegistry& get();

	private:
		explicit AssetRegistry() = default;

	private:
		std::unordered_map<const char*, std::unique_ptr<IAssetType>> m_registeredAssetType;
	};
}