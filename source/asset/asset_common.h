#pragma once

#include <utils/utils.h>

namespace chord
{
	enum class ECompressionMode
	{
		None,
		Lz4,

		MAX
	};

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

		// Snapshot file cache path, not for temp.
		std::string getSnapshotCachePath() const;

		// Bin file cache path, not for temp.
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

	class AssetCompressedMeta
	{
	public:
		ECompressionMode compressionMode;
		int32 rawSize;
		int32 compressionSize;

		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ARCHIVE_ENUM_CLASS(compressionMode);
			ar(rawSize, compressionSize);
		}
	};

	template<typename T>
	static bool saveAsset(const T& in, ECompressionMode compressionMode, const std::filesystem::path& savePath, bool bRequireNoExist = true)
	{
		std::filesystem::path rawSavePath = savePath;
		if (bRequireNoExist && std::filesystem::exists(rawSavePath))
		{
			LOG_ERROR("Meta data {} already exist, make sure never import save resource at same folder!",
				utf8::utf16to8(rawSavePath.u16string()));
			return false;
		}

		std::string rawData;
		{
			std::stringstream ss;
			cereal::BinaryOutputArchive archive(ss);
			archive(in);

			// Exist copy construct.
			rawData = std::move(ss.str());
		}

		AssetCompressedMeta meta;
		meta.compressionMode = compressionMode;
		meta.rawSize = (int32)rawData.size();

		std::string compressedData;
		if (compressionMode == ECompressionMode::Lz4)
		{
			compressedData.resize(LZ4_compressBound((int32)rawData.size()));
			meta.compressionSize = LZ4_compress_default(
				rawData.c_str(),
				compressedData.data(),
				(int32)rawData.size(),
				(int32)compressedData.size());

			compressedData.resize(meta.compressionSize);
		}
		else if (meta.compressionMode == ECompressionMode::None)
		{
			meta.compressionSize = meta.rawSize;
			compressedData = std::move(rawData);
		}
		else
		{
			checkEntry();
		}
		check(compressedData.size() == meta.compressionSize);

		{
			std::ofstream os(rawSavePath, std::ios::binary);
			cereal::BinaryOutputArchive archive(os);
			archive(meta, compressedData);
		}
		return true;
	}

	template<typename T>
	static bool loadAsset(T& out, const std::filesystem::path& savePath)
	{
		if (!std::filesystem::exists(savePath))
		{
			LOG_ERROR("Asset data {} miss!", utf8::utf16to8(savePath.u16string()));
			return false;
		}

		AssetCompressedMeta meta;
		std::string compressedData;
		{
			std::ifstream is(savePath, std::ios::binary);
			cereal::BinaryInputArchive archive(is);
			archive(meta, compressedData);

			check(meta.compressionSize == compressedData.size());
		}

		// Allocate raw data memory.
		std::string rawData;
		if (meta.compressionMode == ECompressionMode::Lz4)
		{
			rawData.resize(meta.rawSize);

			const int32 rawSize = LZ4_decompress_safe(compressedData.data(), rawData.data(), meta.compressionSize, meta.rawSize);
			check(decompressSize == meta.rawSize);
		}
		else if (meta.compressionMode == ECompressionMode::None)
		{
			// Just move compression data to raw data.
			rawData = std::move(compressedData);
			check(meta.compressionSize == meta.rawSize);
		}
		else
		{
			checkEntry();
		}

		{
			std::stringstream ss;
			// Exist copy-construct.
			ss << std::move(rawData);
			cereal::BinaryInputArchive archive(ss);
			archive(out)
		}

		return true;
	}


	// Asset type meta info, register in runtime.
	class IAssetType
	{
	public:
		std::string name;
		std::string icon;
		std::string decoratedName;

		// Asset import config.
		struct
		{
			bool bImportable;
			std::string rawDataExtension;
		} importConfigs;
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