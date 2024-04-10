#pragma once

#include <utils/utils.h>

namespace chord
{
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