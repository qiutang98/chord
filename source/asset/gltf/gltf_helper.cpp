#include <nlohmann/json.hpp>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_INCLUDE_JSON 
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#include <tinygltf/tiny_gltf.h>

#include <asset/gltf/gltf_helper.h>
#include <asset/gltf/gltf.h>

#include <asset/asset.h>
#include <asset/texture/texture.h>
#include <asset/texture/helper.h>
#include <graphics/helper.h>
#include <graphics/uploader.h>
#include <application/application.h>


#include <ui/ui_helper.h>
#include <asset/texture/texture.h>
#include <project.h>


namespace chord
{
	// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_specular/README.md
	constexpr auto KHR_MATERIALS_SPECULAR_EXTENSION_NAME = "KHR_materials_specular";

	// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_materials_sheen/README.md
	constexpr auto KHR_MATERIALS_SHEEN_EXTENSION_NAME = "KHR_materials_sheen";

	// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_anisotropy/README.md
	constexpr auto KHR_MATERIALS_ANISOTROPY_EXTENSION_NAME = "KHR_materials_anisotropy";

	// https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_materials_clearcoat/README.md
	constexpr auto KHR_MATERIALS_CLEARCOAT_EXTENSION_NAME = "KHR_materials_clearcoat";

	// https://github.com/DassaultSystemes-Technology/glTF/tree/KHR_materials_volume/extensions/2.0/Khronos/KHR_materials_transmission
	constexpr auto KHR_MATERIALS_TRANSMISSION_EXTENSION_NAME = "KHR_materials_transmission";

	// https://github.com/DassaultSystemes-Technology/glTF/tree/KHR_materials_volume/extensions/2.0/Khronos/KHR_materials_volume
	constexpr auto KHR_MATERIALS_VOLUME_EXTENSION_NAME = "KHR_materials_volume";

	// Helper load texture id.
	static inline void getTexId(const tinygltf::Value& value, const std::string& name, int32& val)
	{
		if (value.Has(name))
		{
			val = value.Get(name).Get("index").Get<int32>();
		}
	}

	void uiDrawImportConfig(GLTFAssetImportConfigRef config)
	{

	}

	bool importFromConfig(GLTFAssetImportConfigRef config)
	{
		const std::filesystem::path& srcPath = config->importFilePath;
		const std::filesystem::path& savePath = config->storeFilePath;
		const auto& projectPaths = Project::get().getPath();
		auto& assetManager = Application::get().getAssetManager();
		const auto& meta = GLTFAsset::kAssetTypeMeta;
		const auto srcBaseDir = srcPath.parent_path();

		tinygltf::Model model;
		{
			tinygltf::TinyGLTF tcontext;
			std::string warning;
			std::string error;

			auto ext = srcPath.extension().string();
			bool bSuccess = false;
			if (ext == ".gltf")
			{
				bSuccess = tcontext.LoadASCIIFromFile(&model, &error, &warning, srcPath.string());
			}
			else if (ext == ".glb")
			{
				bSuccess = tcontext.LoadBinaryFromFile(&model, &error, &warning, srcPath.string());
			}

			if (!warning.empty()) { LOG_WARN("GLTF '{0} import exist some warnings: '{1}'.", utf8::utf16to8(srcPath.u16string()), warning); }
			if (!error.empty()) { LOG_ERROR("GLTF '{0} import exist some errors: '{1}'.", utf8::utf16to8(srcPath.u16string()), error); }

			if (!bSuccess) { return false; }
		}

		std::string assetNameUtf8 = utf8::utf16to8(savePath.filename().u16string());
		if (std::filesystem::exists(savePath))
		{
			LOG_ERROR("Path {0} already exist, asset {1} import fail!", utf8::utf16to8(savePath.u16string()), assetNameUtf8);
			return false;
		}

		if (!std::filesystem::create_directory(savePath))
		{
			LOG_ERROR("Folder {0} create failed, asset {1} import fail!", utf8::utf16to8(savePath.u16string()), assetNameUtf8);
			return false;
		}

		// Import all images in gltf.
		{
			const auto imageFolderPath = savePath / "images";
			std::filesystem::create_directory(imageFolderPath);

			// Collected srgb by material usage.
			std::set<int32> srgbTexturesMap;
			std::map<int32, float> alphaCoverageMap;

			// Collect all material textures channel usage state.
			struct TextureChannelUsage
			{
				bool bCompression = true; // When some material required no compression, we set it false.
				                          // Combine with operator &&

				math::bvec4 rgba = { false, false, false, false };

				ETextureFormat getFormat(bool b16Bit) const
				{
					if (b16Bit)
					{
						// TODO: Support in gltf.
						checkEntry();
					}

					// 8 bit type.
					if (rgba.r && rgba.g && rgba.b)
					{
						return bCompression 
							? (rgba.a ? ETextureFormat::BC3 : ETextureFormat::BC1)
							: ETextureFormat::R8G8B8A8;
					}

					if (rgba.r && rgba.g)
					{
						return bCompression ? ETextureFormat::BC5 : ETextureFormat::R8G8;
					}

					if (rgba.r)
					{
						return bCompression ? ETextureFormat::BC4R8 : ETextureFormat::R8;
					}
					if (rgba.g)
					{
						return bCompression ? ETextureFormat::BC4G8 : ETextureFormat::G8;
					}
					if (rgba.b)
					{
						return bCompression ? ETextureFormat::BC4B8 : ETextureFormat::B8;
					}
					if (rgba.a)
					{
						return bCompression ? ETextureFormat::BC4A8 : ETextureFormat::A8;
					}

					LOG_FATAL("No suitable format for texture!");
					return ETextureFormat::MAX;
				}
			};
			std::map<int32, TextureChannelUsage> textureChannelUsageMap;

			for (auto& material : model.materials)
			{
				int32 baseColorTexture = material.pbrMetallicRoughness.baseColorTexture.index;
				srgbTexturesMap.insert(baseColorTexture);
				if (material.alphaMode != "OPAQUE")
				{
					alphaCoverageMap[baseColorTexture] = material.alphaCutoff;
				}
				// Base color .rgba channel all used.
				textureChannelUsageMap[baseColorTexture].rgba = { true, true, true, true };

				int32 emissiveTexture = material.emissiveTexture.index;
				srgbTexturesMap.insert(emissiveTexture);

				// Emissive texture use .rgb.
				textureChannelUsageMap[emissiveTexture].rgba.r = true;
				textureChannelUsageMap[emissiveTexture].rgba.g = true;
				textureChannelUsageMap[emissiveTexture].rgba.b = true;

				int32 normalTexture = material.normalTexture.index;

				// Normal texture use .rg.
				textureChannelUsageMap[normalTexture].rgba.r = true;
				textureChannelUsageMap[normalTexture].rgba.g = true;

				// Occlusion texture use .r channel.
				int32 occlusionTexture = material.occlusionTexture.index;
				textureChannelUsageMap[occlusionTexture].rgba.r = true;

				// Metallic roughness texture use .gb channel.
				int32 metallicRoughnessTexture = material.pbrMetallicRoughness.metallicRoughnessTexture.index;
				textureChannelUsageMap[metallicRoughnessTexture].rgba.g = true;
				textureChannelUsageMap[metallicRoughnessTexture].rgba.b = true;

				if (metallicRoughnessTexture != occlusionTexture)
				{
					LOG_WARN("Generally metallicRoughnessTexture same with occlusionTexture for best performance.");
				}

				if (material.extensions.contains(KHR_MATERIALS_SPECULAR_EXTENSION_NAME))
				{
					const auto& ext = material.extensions[KHR_MATERIALS_SPECULAR_EXTENSION_NAME];

					int32 specularColorTexture;
					int32 specularTexture;

					getTexId(ext, "specularColorTexture", specularColorTexture);
					getTexId(ext, "specularTexture", specularTexture);

					srgbTexturesMap.insert(specularColorTexture);

					// Specular color texture use rgb.
					textureChannelUsageMap[specularColorTexture].rgba.r = true;
					textureChannelUsageMap[specularColorTexture].rgba.g = true;
					textureChannelUsageMap[specularColorTexture].rgba.b = true;

					// specular texture use a.
					textureChannelUsageMap[specularTexture].rgba.a = true;

					if (specularTexture != specularColorTexture)
					{
						LOG_WARN("Generally specularTexture same with specularColorTexture for best performance.");
					}
				}

				if (material.extensions.contains(KHR_MATERIALS_ANISOTROPY_EXTENSION_NAME))
				{
					const auto& ext = material.extensions[KHR_MATERIALS_ANISOTROPY_EXTENSION_NAME];
					int32 texture;
					getTexId(ext, "anisotropyTexture", texture);

					// Red and green channels represent the anisotropy direction in [-1, 1] tangent, bitangent space, to be rotated by anisotropyRotation. 
					// The blue channel contains strength as [0, 1] to be multiplied by anisotropyStrength.
					// Anisotropy texture use rgb.
					textureChannelUsageMap[texture].rgba.r = true;
					textureChannelUsageMap[texture].rgba.g = true;
					textureChannelUsageMap[texture].rgba.b = true;
				}

				if (material.extensions.contains(KHR_MATERIALS_CLEARCOAT_EXTENSION_NAME))
				{
					const auto& ext = material.extensions[KHR_MATERIALS_CLEARCOAT_EXTENSION_NAME];
					int32 clearcoatTexture;
					int32 clearcoatRoughnessTexture;
					int32 clearcoatNormalTexture;
					getTexId(ext, "clearcoatTexture", clearcoatTexture);
					getTexId(ext, "clearcoatRoughnessTexture", clearcoatRoughnessTexture);
					getTexId(ext, "clearcoatNormalTexture", clearcoatNormalTexture);

					textureChannelUsageMap[clearcoatTexture].rgba.r = true;
					textureChannelUsageMap[clearcoatRoughnessTexture].rgba.g = true;

					textureChannelUsageMap[clearcoatNormalTexture].rgba.r = true;
					textureChannelUsageMap[clearcoatNormalTexture].rgba.g = true;

					if (clearcoatTexture == clearcoatNormalTexture || clearcoatRoughnessTexture == clearcoatNormalTexture)
					{
						LOG_WARN("Repeated texture used in clear coat material '{}'.", material.name);
					}
				}

				if (material.extensions.contains(KHR_MATERIALS_SHEEN_EXTENSION_NAME))
				{
					const auto& ext = material.extensions[KHR_MATERIALS_SHEEN_EXTENSION_NAME];
					int32 colorTexture;
					int32 roughnessTexture;

					getTexId(ext, "sheenColorTexture", colorTexture);
					getTexId(ext, "sheenRoughnessTexture", roughnessTexture);

					srgbTexturesMap.insert(colorTexture);

					textureChannelUsageMap[colorTexture].rgba.r = true;
					textureChannelUsageMap[colorTexture].rgba.g = true;
					textureChannelUsageMap[colorTexture].rgba.b = true;

					textureChannelUsageMap[roughnessTexture].rgba.a = true;
					if (roughnessTexture != colorTexture)
					{
						LOG_WARN("Generally sheenColorTexture same with sheenRoughnessTexture for best performance.");
					}
				}

				if (material.extensions.contains(KHR_MATERIALS_TRANSMISSION_EXTENSION_NAME))
				{
					const auto& ext = material.extensions[KHR_MATERIALS_TRANSMISSION_EXTENSION_NAME];
					int32 texture;
					getTexId(ext, "transmissionTexture", texture);

					textureChannelUsageMap[texture].rgba.r = true;
				}

				if (material.extensions.contains(KHR_MATERIALS_VOLUME_EXTENSION_NAME))
				{
					const auto& ext = material.extensions[KHR_MATERIALS_VOLUME_EXTENSION_NAME];
					int32 texture;
					getTexId(ext, "thicknessTexture", texture);

					textureChannelUsageMap[texture].rgba.g = true;
				}
			}

			// Now load all images.
			for (int32 imageIndex = 0; imageIndex < model.images.size(); imageIndex ++)
			{
				const auto& gltfImage = model.images[imageIndex];
				const bool bSrgb = srgbTexturesMap.contains(imageIndex);
				const bool bAlphaCoverage = alphaCoverageMap.contains(imageIndex);

				const bool bExistChannelCollect = textureChannelUsageMap.contains(imageIndex);
				if (!bExistChannelCollect)
				{
					LOG_WARN("Texture '{}' no exist channel collect, may cause error format select, we skip it.", gltfImage.name);
				}

				std::string uriDecoded;
				tinygltf::URIDecode(gltfImage.uri, &uriDecoded, nullptr);

				std::filesystem::path uri = std::filesystem::path(uriDecoded);
				std::string extension = uri.extension().string();
				std::string imgName = uri.filename().string();

				std::filesystem::path imgUri = srcBaseDir / uri;


				if (extension.empty())
				{
					// Loaded from glb, first extract to png.
					if (imgName.empty())
					{
						imgName = generateUUID() + ".png";
					}
					std::filesystem::path tempSavedTexturesPath = srcBaseDir / imgName;

					int32 channelNum = gltfImage.component == -1 ? 4 : gltfImage.component;
					if (gltfImage.bits != 8 || gltfImage.bits != -1)
					{
						// TODO: Support me.
						LOG_ERROR("Current not support embed 16 bit or 32 bit image.");
					}
					stbi_write_png(tempSavedTexturesPath.string().c_str(), gltfImage.width, gltfImage.height,
						channelNum, gltfImage.image.data(), gltfImage.width * channelNum);

					const bool b16Bit = (gltfImage.bits == 16);

					auto textureAssetImportConfig = std::make_shared<TextureAssetImportConfig>();

					textureAssetImportConfig->importFilePath = tempSavedTexturesPath;
					textureAssetImportConfig->storeFilePath  = imageFolderPath / std::filesystem::path(imgName).replace_extension();
					textureAssetImportConfig->bSRGB = bSrgb;
					textureAssetImportConfig->bGenerateMipmap = true;
					textureAssetImportConfig->alphaMipmapCutoff = bAlphaCoverage ? alphaCoverageMap[imageIndex] : 1.0f;
					textureAssetImportConfig->format = textureChannelUsageMap[imageIndex].getFormat(b16Bit);

					if (!TextureAsset::kAssetTypeMeta.importConfig.importAssetFromConfig(textureAssetImportConfig))
					{
						LOG_ERROR("Fail to import texture {}.", utf8::utf16to8(imgUri.u16string()))
					}

					std::filesystem::remove(tempSavedTexturesPath);
				}
				else
				{
					// Read the header again to check if it has 16 bit data, e.g. for a heightmap.
					const bool b16Bit = stbi_is_16_bit(imgUri.string().c_str());

					// Loaded from file.
					auto textureAssetImportConfig = std::make_shared<TextureAssetImportConfig>();

					textureAssetImportConfig->importFilePath = imgUri;
					textureAssetImportConfig->storeFilePath = imageFolderPath / uri.filename().replace_extension();
					textureAssetImportConfig->bSRGB = bSrgb;
					textureAssetImportConfig->bGenerateMipmap = true;
					textureAssetImportConfig->alphaMipmapCutoff = bAlphaCoverage ? alphaCoverageMap[imageIndex] : 1.0f;
					textureAssetImportConfig->format = textureChannelUsageMap[imageIndex].getFormat(b16Bit);

					if (!TextureAsset::kAssetTypeMeta.importConfig.importAssetFromConfig(textureAssetImportConfig))
					{
						LOG_ERROR("Fail to import texture {}.", utf8::utf16to8(imgUri.u16string()))
					}
				}
			}
		}

		// Import all materials.
		{

		}




		return true;
	}

	AssetTypeMeta GLTFAsset::createTypeMeta()
	{
		AssetTypeMeta result;
		// 
		result.name = "GLTF";
		result.icon = ICON_FA_TRUCK_FIELD;
		result.decoratedName = std::string("  ") + ICON_FA_TRUCK_FIELD + "    GLTF";

		//
		result.suffix = ".assetgltf";

		// Import config.
		{
			result.importConfig.bImportable = true;
			result.importConfig.rawDataExtension = "gltf,glb";
			result.importConfig.getAssetImportConfig = [] { return std::make_shared<GLTFAssetImportConfig>(); };
			result.importConfig.uiDrawAssetImportConfig = [](IAssetImportConfigRef config)
			{
				uiDrawImportConfig(std::static_pointer_cast<GLTFAssetImportConfig>(config));
			};
			result.importConfig.importAssetFromConfig = [](IAssetImportConfigRef config)
			{
				return importFromConfig(std::static_pointer_cast<GLTFAssetImportConfig>(config));
			};
		}


		return result;
	};
}