#pragma once

#include <utils/utils.h>
#include <utils/log.h>
#include <project.h>

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

		// Generate temp save info. NOTE: All non store in disk asset is temp.
		static AssetSaveInfo buildTemp(const u16str& name);

		// Generate builtin save info, also is temp file.
		static AssetSaveInfo buildBuiltin(const std::string& name);

		// Generate project asset relative path.
		static AssetSaveInfo buildRelativeAsset(const std::filesystem::path& savePath);

		// Default compare.
		bool operator==(const AssetSaveInfo& o) const
		{
			return m_name == o.m_name && m_storeFolder == o.m_storeFolder;
		}

	public:
		const uint64 hash() const;

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

		// Asset name only, std::filesystem::path.filename.
		const auto& getName() const
		{
			return m_name;
		}

		// Asset store folder relative to project asset folder.
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

		// Snapshot file cache path hash name, not for temp.
		std::string getSnapshotCachePath() const;

		// Bin file cache path hash name, not for temp.
		std::string getBinCachePath() const;

	private:
		u16str m_name; // Asset name only, std::filesystem::path.filename.
		u16str m_storeFolder; // Asset store folder relative to project asset folder.

	public:
		template<class Ar> void serialize(Ar& ar)
		{
			ar(m_name, m_storeFolder);
		}
	};

	// Asset import config.
	struct IAssetImportConfig
	{
		std::filesystem::path importFilePath;
		std::filesystem::path storeFilePath;


		// Create asset texture.
		AssetSaveInfo getSaveInfo(const std::string& suffix) const
		{
			const std::filesystem::path& srcPath = this->importFilePath;
			const std::filesystem::path& savePath = this->storeFilePath;

			// Build texture ptr.
			const auto name = savePath.filename().u16string() + utf8::utf8to16(suffix);
			const auto relativePath =
				buildRelativePath(Project::get().getPath().assetPath.u16(), savePath.parent_path());

			// Create asset texture.
			return AssetSaveInfo(name, relativePath);
		}
	};
	using IAssetImportConfigRef = std::shared_ptr<IAssetImportConfig>;

	// Asset type meta info, register in runtime.
	class AssetTypeMeta
	{
	public:
		std::string name;
		std::string icon;
		std::string decoratedName;

		// Store asset suffix.
		std::string suffix;

		// Asset import config.
		struct
		{
			bool bImportable;
			std::string rawDataExtension;

			std::function<IAssetImportConfigRef()> getAssetImportConfig = nullptr;
			std::function<void(IAssetImportConfigRef)> uiDrawAssetImportConfig = nullptr;
			std::function<bool(IAssetImportConfigRef)> importAssetFromConfig = nullptr;
		} importConfig;
	};
}