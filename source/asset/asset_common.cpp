#include <asset/asset_common.h>
#include <utils/cvar.h>
#include <project.h>

namespace chord
{
	static AutoCVar<uint32> cVarAssetVersion(
		"r.asset.version",
		0,
		"Asset version.",
		EConsoleVarFlags::ReadOnly
	);
	const uint32 chord::kAssetVersion = cVarAssetVersion.get();

	const char* AssetSaveInfo::kTempFolderStartChar  = "*";
	const char* AssetSaveInfo::kBuiltinFileStartChar = "~";

	AssetSaveInfo::AssetSaveInfo(const u16str& name, const u16str& storeFolder)
		: m_name(name), m_storeFolder(storeFolder)
	{

	}

	AssetSaveInfo AssetSaveInfo::buildTemp(const u16str& name)
	{
		return AssetSaveInfo(name, u16str(kTempFolderStartChar + generateUUID()));
	}

	AssetSaveInfo AssetSaveInfo::buildBuiltin(const std::string& name)
	{
		check(name.starts_with(kBuiltinFileStartChar));
		return AssetSaveInfo(u16str(name), u16str(kTempFolderStartChar + generateUUID()));
	}

	AssetSaveInfo AssetSaveInfo::buildRelativeAsset(const std::filesystem::path& savePath)
	{
		auto fileName = savePath.filename();

		auto saveFolder = savePath;
		saveFolder.remove_filename();

		const auto relativePath = buildRelativePath(Project::get().getPath().assetPath.u16(), saveFolder);
		return AssetSaveInfo(fileName.u16string(), relativePath);
	}

	bool AssetSaveInfo::isTemp() const
	{
		return m_storeFolder.u8().starts_with(kTempFolderStartChar);
	}

	bool AssetSaveInfo::isBuiltin() const
	{
		return isTemp() && m_name.u8().starts_with(kBuiltinFileStartChar);
	}

	const std::filesystem::path AssetSaveInfo::relativeAssetStorePath() const
	{
		return std::filesystem::path(m_storeFolder.u16()) / m_name.u16();
	}

	bool AssetSaveInfo::setName(const u16str& newValue)
	{
		if (m_name != newValue)
		{
			m_name = newValue;
			return true;
		}

		return false;
	}

	bool AssetSaveInfo::setStoreFolder(const u16str& newValue)
	{
		if (m_storeFolder != newValue)
		{
			m_storeFolder = newValue;
			return true;
		}

		return false;
	}

	std::string AssetSaveInfo::getSnapshotCachePath() const
	{
		check(!isTemp());
		const auto suffix = "_snapshot";

		const auto hashID = std::hash<UUID>{}(utf8::utf16to8(relativeAssetStorePath().u16string()) + suffix);
		return std::to_string(hashID) + suffix;
	}

	std::string AssetSaveInfo::getBinCachePath() const
	{
		check(!isTemp());
		const auto suffix = "_bin";

		const auto hashID = std::hash<UUID>{}(utf8::utf16to8(relativeAssetStorePath().u16string()) + suffix);
		return std::to_string(hashID) + suffix;
	}

	const std::filesystem::path AssetSaveInfo::path() const
	{
		std::filesystem::path path = Project::get().getPath().assetPath.u16();
		return path / relativeAssetStorePath();
	}

	bool AssetSaveInfo::alreadyInDisk() const
	{
		return std::filesystem::exists(path());
	}

	AssetRegistry& AssetRegistry::get()
	{
		static AssetRegistry registry;
		return registry;
	}

}

