#pragma once

#include <utils/utils.h>
#include <asset/asset_common.h>

namespace chord::gltf
{
	
	extern bool isGLTFExtensionSupported(const std::string& name);

	extern std::unordered_map<int32, AssetSaveInfo> importMaterialUsedImages(
		const std::filesystem::path& srcPath,
		const std::filesystem::path& savePath,
		const tinygltf::Model& model);

	extern std::unordered_map<int32, AssetSaveInfo> importMaterials(
		const std::filesystem::path& srcPath,
		const std::filesystem::path& savePath,
		const std::unordered_map<int32, AssetSaveInfo>& importedImageMap,
		const tinygltf::Model& model);
}
