#include <asset/gltf/gltf_material.h>
#include <asset/texture/helper.h>
#include <asset/texture/texture.h>
#include <application/application.h>
#include <asset/gltf/gltf.h>

using namespace chord;

enum class EKHRGLTFExtension : uint8
{
	LightPunctual = 0,
	TextureTransform,
	MaterialSpecular,
	MaterialUnlit,
	MaterialAnisotropy,
	MaterialIOR,
	MaterialVolume,
	MaterialTransmission,
	TextureBasisu,
	MaterialClearCoat,
	MaterialSheen,
	MAX
};

static const std::vector<std::string> kSupportedGLTFExtension =
{
	"KHR_lights_punctual", // 
	"KHR_texture_transform", // https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_texture_transform
	"KHR_materials_specular", // https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_specular/README.md
	"KHR_materials_unlit", // https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_materials_unlit
	"KHR_materials_anisotropy",	// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_anisotropy/README.md
	"KHR_materials_ior", // https://github.com/DassaultSystemes-Technology/glTF/tree/KHR_materials_ior/extensions/2.0/Khronos/KHR_materials_ior
	"KHR_materials_volume",// https://github.com/DassaultSystemes-Technology/glTF/tree/KHR_materials_volume/extensions/2.0/Khronos/KHR_materials_volume
	"KHR_materials_transmission",// https://github.com/DassaultSystemes-Technology/glTF/tree/KHR_materials_volume/extensions/2.0/Khronos/KHR_materials_transmission
	"KHR_texture_basisu",// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_basisu/README.md
	"KHR_materials_clearcoat", // https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_materials_clearcoat/README.md
	"KHR_materials_sheen", // https://github.com/KhronosGroup/glTF/blob/master/extensions/2.0/Khronos/KHR_materials_sheen/README.md
};

static inline const std::string& getGLTFExtension(EKHRGLTFExtension ext)
{
	return kSupportedGLTFExtension[(int)ext];
}

bool gltf::isGLTFExtensionSupported(const std::string& name)
{
	for (const auto& ext : kSupportedGLTFExtension)
	{
		if (name == ext) { return true; }
	}

	return false;
}

// Helper load texture id.
static inline void getTexId(const tinygltf::Value& value, const std::string& name, int32& val)
{
	if (value.Has(name))
	{
		val = value.Get(name).Get("index").Get<int32>();
	}
}

// Collect all material image channel usage state.
struct ImageChannelUsage
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

std::unordered_map<int32, AssetSaveInfo> gltf::importMaterialUsedImages(
	const std::filesystem::path& srcPath,
	const std::filesystem::path& savePath,
	const tinygltf::Model& model)
{
	const auto& projectPaths = Project::get().getPath();
	auto& assetManager = Application::get().getAssetManager();
	const auto srcBaseDir = srcPath.parent_path();

	std::unordered_map<int32, AssetSaveInfo> importedImages;

	const auto imageFolderPath = savePath / "images";
	std::filesystem::create_directory(imageFolderPath);

	// Collected srgb by material usage.
	std::set<int32> srgbImagesMap;
	std::map<int32, float> alphaCoverageImagesMap;


	std::map<int32, ImageChannelUsage> imageChannelUsageMap;
	for (auto& material : model.materials)
	{
		if (material.pbrMetallicRoughness.baseColorTexture.index != -1)
		{
			int32 baseColorImageIndex = model.textures.at(material.pbrMetallicRoughness.baseColorTexture.index).source;
			srgbImagesMap.insert(baseColorImageIndex);
			if (material.alphaMode != "OPAQUE")
			{
				alphaCoverageImagesMap[baseColorImageIndex] = material.alphaCutoff;
			}
			// Base color .rgba channel all used.
			imageChannelUsageMap[baseColorImageIndex].rgba = { true, true, true, true };
		}


		if (material.emissiveTexture.index != -1)
		{
			int32 emissiveImageIndex = model.textures.at(material.emissiveTexture.index).source;
			srgbImagesMap.insert(emissiveImageIndex);

			// Emissive image use .rgb.
			imageChannelUsageMap[emissiveImageIndex].rgba.r = true;
			imageChannelUsageMap[emissiveImageIndex].rgba.g = true;
			imageChannelUsageMap[emissiveImageIndex].rgba.b = true;
		}

		if (material.normalTexture.index != -1)
		{
			int32 normalImageIndex = model.textures.at(material.normalTexture.index).source;

			// Normal image use .rg.
			imageChannelUsageMap[normalImageIndex].rgba.r = true;
			imageChannelUsageMap[normalImageIndex].rgba.g = true;
		}

		if (material.occlusionTexture.index != -1)
		{
			// Occlusion image use .r channel.
			int32 occlusionImageIndex = model.textures.at(material.occlusionTexture.index).source;
			imageChannelUsageMap[occlusionImageIndex].rgba.r = true;
		}

		if (material.pbrMetallicRoughness.metallicRoughnessTexture.index != -1)
		{
			// Metallic roughness image use .gb channel.
			int32 metallicRoughnessImageIndex = model.textures.at(material.pbrMetallicRoughness.metallicRoughnessTexture.index).source;

			if (material.occlusionTexture.index == -1)
			{
				imageChannelUsageMap[metallicRoughnessImageIndex].rgba.r = true;
			}

			imageChannelUsageMap[metallicRoughnessImageIndex].rgba.g = true;
			imageChannelUsageMap[metallicRoughnessImageIndex].rgba.b = true;
		}

		if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialSpecular)))
		{
			const auto& ext = material.extensions.at(getGLTFExtension(EKHRGLTFExtension::MaterialSpecular));

			int32 specularColorTexture;
			int32 specularTexture;

			getTexId(ext, "specularColorTexture", specularColorTexture);
			getTexId(ext, "specularTexture", specularTexture);

			int32 specularColorImage = model.textures.at(specularColorTexture).source;
			int32 specularImage = model.textures.at(specularTexture).source;

			srgbImagesMap.insert(specularColorImage);

			// Specular color texture use rgb.
			imageChannelUsageMap[specularColorImage].rgba.r = true;
			imageChannelUsageMap[specularColorImage].rgba.g = true;
			imageChannelUsageMap[specularColorImage].rgba.b = true;

			// specular texture use a.
			imageChannelUsageMap[specularImage].rgba.a = true;

			if (specularImage != specularColorImage)
			{
				LOG_WARN("Generally specularTexture same with specularColorTexture for best performance.");
			}
		}

		if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialAnisotropy)))
		{
			const auto& ext = material.extensions.at(getGLTFExtension(EKHRGLTFExtension::MaterialAnisotropy));
			int32 texture;
			getTexId(ext, "anisotropyTexture", texture);

			int32 imageIndex = model.textures.at(texture).source;

			// Red and green channels represent the anisotropy direction in [-1, 1] tangent, bitangent space, to be rotated by anisotropyRotation. 
			// The blue channel contains strength as [0, 1] to be multiplied by anisotropyStrength.
			// Anisotropy texture use rgb.
			imageChannelUsageMap[imageIndex].rgba.r = true;
			imageChannelUsageMap[imageIndex].rgba.g = true;
			imageChannelUsageMap[imageIndex].rgba.b = true;
		}

		if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialClearCoat)))
		{
			const auto& ext = material.extensions.at(getGLTFExtension(EKHRGLTFExtension::MaterialClearCoat));

			int32 clearcoatTexture;
			int32 clearcoatRoughnessTexture;
			int32 clearcoatNormalTexture;

			getTexId(ext, "clearcoatTexture", clearcoatTexture);
			getTexId(ext, "clearcoatRoughnessTexture", clearcoatRoughnessTexture);
			getTexId(ext, "clearcoatNormalTexture", clearcoatNormalTexture);

			int32 clearcoatImage = model.textures.at(clearcoatTexture).source;
			int32 clearcoatRoughnessImage = model.textures.at(clearcoatRoughnessTexture).source;
			int32 clearcoatNormalImage = model.textures.at(clearcoatNormalTexture).source;

			imageChannelUsageMap[clearcoatImage].rgba.r = true;
			imageChannelUsageMap[clearcoatRoughnessImage].rgba.g = true;

			imageChannelUsageMap[clearcoatNormalImage].rgba.r = true;
			imageChannelUsageMap[clearcoatNormalImage].rgba.g = true;

			if (clearcoatImage == clearcoatNormalImage || clearcoatRoughnessImage == clearcoatNormalImage)
			{
				LOG_ERROR("Repeated texture used in clear coat material '{}', will cause some shading error here.", material.name);
			}
		}

		if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialSheen)))
		{
			const auto& ext = material.extensions.at(getGLTFExtension(EKHRGLTFExtension::MaterialSheen));
			int32 colorTexture;
			int32 roughnessTexture;

			getTexId(ext, "sheenColorTexture", colorTexture);
			getTexId(ext, "sheenRoughnessTexture", roughnessTexture);

			int32 colorImage = model.textures.at(colorTexture).source;
			int32 roughnessImage = model.textures.at(roughnessTexture).source;

			srgbImagesMap.insert(colorImage);

			imageChannelUsageMap[colorImage].rgba.r = true;
			imageChannelUsageMap[colorImage].rgba.g = true;
			imageChannelUsageMap[colorImage].rgba.b = true;

			imageChannelUsageMap[roughnessImage].rgba.a = true;
			if (roughnessImage != colorImage)
			{
				LOG_WARN("Generally sheenColorTexture same with sheenRoughnessTexture for best performance.");
			}
		}

		if (0 && material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialTransmission)))
		{
			const auto& ext = material.extensions.at(getGLTFExtension(EKHRGLTFExtension::MaterialTransmission));
			int32 texture;
			getTexId(ext, "transmissionTexture", texture);
			int32 imageIndex = model.textures.at(texture).source;

			imageChannelUsageMap[imageIndex].rgba.r = true;
		}

		if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialVolume)))
		{
			const auto& ext = material.extensions.at(getGLTFExtension(EKHRGLTFExtension::MaterialVolume));
			int32 texture;
			getTexId(ext, "thicknessTexture", texture);
			int32 imageIndex = model.textures.at(texture).source;

			imageChannelUsageMap[imageIndex].rgba.g = true;
		}
	}

	struct CompositeDetailed
	{
		int32 srcImage;
		int32 srcImageChannel; // 0 is R, 1 is G, 2 is B, 3 is A.
		int32 destImageChannel;
	};
	std::map<int32, std::vector<CompositeDetailed>> pendingCompositeImages;
	std::map<int32, int32> skipLoadImage; // The image already composite in other image, use this map to indexing.
	for (auto& material : model.materials)
	{
		if (material.pbrMetallicRoughness.metallicRoughnessTexture.index != -1 && material.occlusionTexture.index != -1)
		{
			tinygltf::Texture metallicRoughnessTexture = model.textures.at(material.pbrMetallicRoughness.metallicRoughnessTexture.index);
			tinygltf::Texture occlusionTexture = model.textures.at(material.occlusionTexture.index);

			if (!skipLoadImage.contains(occlusionTexture.source))
			{
				if (metallicRoughnessTexture.source != occlusionTexture.source)
				{
					{
						auto sourceSwitch = metallicRoughnessTexture;
						sourceSwitch.source = occlusionTexture.source;
						checkMsgf(sourceSwitch == occlusionTexture,
							"Try to compositing metallicRoughnessTexture and occlusionTexture for '{}', but it's sampler or other state no same!", material.name);
					}

					if (imageChannelUsageMap[metallicRoughnessTexture.source].rgba.r)
					{
						LOG_ERROR("Try to composite occlusionTexture in metallicRoughnessTexture.r, but it already used, this is a logic error.");
					}
					else
					{
						LOG_TRACE("Composing occlusionTexture in metallicRoughnessTexture .r channel for best performance...");

						// Occlusion texture skip load, it use metallicRoughnessTexture.r
						skipLoadImage[occlusionTexture.source] = metallicRoughnessTexture.source;

						CompositeDetailed detail{ };
						detail.srcImage = occlusionTexture.source;
						detail.srcImageChannel = 0;
						detail.destImageChannel = 0;
						pendingCompositeImages[metallicRoughnessTexture.source].push_back(detail);

						imageChannelUsageMap[metallicRoughnessTexture.source].rgba.r = true;
					}
				}
			}
		}
	}

	// Composition.
	std::unordered_map<int32, std::filesystem::path> compositedSaveImage;
	for (int32 i = 0; i < model.images.size(); i++)
	{
		if (!pendingCompositeImages.contains(i))
		{
			continue;
		}

		const auto& pendingCompositions = pendingCompositeImages[i];
		const auto& destImage = model.images[i];

		std::vector<uint8> compositeMemory{ };
		compositeMemory.resize(destImage.width * destImage.height * 4);
		std::filesystem::path destUri;
		{
			std::string uriDecoded;
			tinygltf::URIDecode(destImage.uri, &uriDecoded, nullptr);
			destUri = std::filesystem::path(uriDecoded);
			std::string extension = destUri.extension().string();

			if (extension.empty())
			{
				memcpy(compositeMemory.data(), destImage.image.data(), compositeMemory.size());
			}
			else
			{
				ImageLdr2D ldr{ };
				std::filesystem::path imgUri = srcBaseDir / destUri;
				ldr.fillFromFile(imgUri.string());

				memcpy(compositeMemory.data(), ldr.getPixels(), compositeMemory.size());
			}
		}
		std::string imgName = destUri.filename().string();

		for (const auto& detail : pendingCompositions)
		{
			const auto& srcImage = model.images[detail.srcImage];
			check(detail.destImageChannel <= 3 && detail.destImageChannel >= 0);
			check(detail.srcImageChannel <= 3 && detail.srcImageChannel >= 0);

			if (srcImage.bits != 8 && srcImage.bits != -1) { unimplemented(); }
			if (srgbImagesMap.contains(detail.srcImage)) { unimplemented(); }

			std::string uriDecoded;
			tinygltf::URIDecode(srcImage.uri, &uriDecoded, nullptr);
			std::filesystem::path uri = std::filesystem::path(uriDecoded);
			std::string extension = uri.extension().string();


			std::unique_ptr<ImageLdr2D> imagePtr = nullptr;
			const uint8* srcData = nullptr;
			if (extension.empty())
			{
				// Load from glb.
				srcData = srcImage.image.data();
			}
			else
			{
				imagePtr = std::make_unique<ImageLdr2D>();
				std::filesystem::path imgUri = srcBaseDir / uri;

				imagePtr->fillFromFile(imgUri.string());
				srcData = imagePtr->getPixels();
			}

			std::vector<uint8> sizeFitData{ };
			if (srcImage.width != destImage.width || srcImage.height != destImage.height)
			{
				sizeFitData.resize(destImage.width * destImage.height * 4);
				stbir_resize_uint8(
					srcData, srcImage.width, srcImage.height, 0,
					sizeFitData.data(), destImage.width, destImage.height, 0, 4);

				srcData = sizeFitData.data();
			}

			for (uint i = 0; i < compositeMemory.size(); i += 4)
			{
				compositeMemory[i + detail.destImageChannel] = srcData[i + detail.srcImageChannel];
			}

			imgName += uri.filename().string();
		}

		if (imgName.empty())
		{
			imgName += generateUUID();
		}
		imgName += ".png";

		std::filesystem::path tempSavedTexturesPath = std::filesystem::path(projectPaths.cachePath.u16()) / imgName;

		stbi_write_png(tempSavedTexturesPath.string().c_str(), destImage.width, destImage.height,
			4, compositeMemory.data(), destImage.width * 4);

		compositedSaveImage[i] = tempSavedTexturesPath;
	}

	// Now load all images.
	for (int32 imageIndex = 0; imageIndex < model.images.size(); imageIndex++)
	{
		if (skipLoadImage.contains(imageIndex))
		{
			// Current pass skip image which composite to other image.
			continue;
		}

		const auto& gltfImage = model.images[imageIndex];

		// This texture is srgb encode or not.
		const bool bSrgb = srgbImagesMap.contains(imageIndex);

		// This texture should be alpha coverage or not.
		const bool bAlphaCoverage = alphaCoverageImagesMap.contains(imageIndex);

		// Check it's channel collect.
		const bool bExistChannelCollect = imageChannelUsageMap.contains(imageIndex);
		if (!bExistChannelCollect)
		{
			LOG_ERROR("Texture '{}' no exist channel collect, may cause error format select, we skip it.", gltfImage.name);
			continue;
		}

		std::string uriDecoded;
		tinygltf::URIDecode(gltfImage.uri, &uriDecoded, nullptr);
		std::filesystem::path uri = std::filesystem::path(uriDecoded);
		std::string extension = uri.extension().string();
		std::string imgName = uri.filename().string();

		std::filesystem::path imgUri = srcBaseDir / uri;
		AssetSaveInfo saveInfo;

		const bool bCompositedSaved = compositedSaveImage.contains(imageIndex);
		if (bCompositedSaved)
		{
			imgUri = compositedSaveImage[imageIndex];
		}

		if (extension.empty() && (!bCompositedSaved))
		{
			// Loaded from glb, first extract to png.
			if (imgName.empty())
			{
				imgName = generateUUID() + ".png";
			}
			std::filesystem::path tempSavedTexturesPath = std::filesystem::path(projectPaths.cachePath.u16()) / imgName;

			int32 channelNum = gltfImage.component == -1 ? 4 : gltfImage.component;
			if (gltfImage.bits != 8 && gltfImage.bits != -1)
			{
				LOG_ERROR("Image '{0}' embed with bit {1} not support.", gltfImage.name, gltfImage.bits);
			}
			stbi_write_png(tempSavedTexturesPath.string().c_str(), gltfImage.width, gltfImage.height,
				channelNum, gltfImage.image.data(), gltfImage.width * channelNum);

			const bool b16Bit = (gltfImage.bits == 16);
			auto textureAssetImportConfig = std::make_shared<TextureAssetImportConfig>();

			textureAssetImportConfig->importFilePath = tempSavedTexturesPath;
			textureAssetImportConfig->storeFilePath = imageFolderPath / std::filesystem::path(imgName).replace_extension();
			textureAssetImportConfig->bSRGB = bSrgb;
			textureAssetImportConfig->bGenerateMipmap = true;
			textureAssetImportConfig->alphaMipmapCutoff = bAlphaCoverage ? alphaCoverageImagesMap[imageIndex] : 1.0f;
			textureAssetImportConfig->format = imageChannelUsageMap[imageIndex].getFormat(b16Bit);

			if (!TextureAsset::kAssetTypeMeta.importConfig.importAssetFromConfig(textureAssetImportConfig))
			{
				LOG_ERROR("Fail to import texture {}.", utf8::utf16to8(imgUri.u16string()))
			}
			else
			{
				saveInfo = textureAssetImportConfig->getSaveInfo(TextureAsset::kAssetTypeMeta.suffix);
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
			textureAssetImportConfig->storeFilePath = imageFolderPath / imgUri.filename().replace_extension();
			textureAssetImportConfig->bSRGB = bSrgb;
			textureAssetImportConfig->bGenerateMipmap = true;
			textureAssetImportConfig->alphaMipmapCutoff = bAlphaCoverage ? alphaCoverageImagesMap[imageIndex] : 1.0f;
			textureAssetImportConfig->format = imageChannelUsageMap[imageIndex].getFormat(b16Bit);

			if (!TextureAsset::kAssetTypeMeta.importConfig.importAssetFromConfig(textureAssetImportConfig))
			{
				LOG_ERROR("Fail to import texture {}.", utf8::utf16to8(imgUri.u16string()))
			}
			else
			{
				saveInfo = textureAssetImportConfig->getSaveInfo(TextureAsset::kAssetTypeMeta.suffix);
			}
		}

		// Cache import texture save infos.
		if (!saveInfo.empty())
		{
			importedImages[imageIndex] = saveInfo;
		}
	}

	for (auto& compositionTempPath : compositedSaveImage)
	{
		std::filesystem::remove(compositionTempPath.second);
	}

	// Skip image use it's composite image save info.
	for (int32 imageIndex = 0; imageIndex < model.images.size(); imageIndex++)
	{
		if (skipLoadImage.contains(imageIndex))
		{
			importedImages[imageIndex] = importedImages[skipLoadImage.at(imageIndex)];
		}
	}

	return std::move(importedImages);
}

std::unordered_map<int32, AssetSaveInfo> gltf::importMaterials(
	const std::filesystem::path& srcPath,
	const std::filesystem::path& savePath,
	const std::unordered_map<int32, AssetSaveInfo>& importedImageMap,
	const tinygltf::Model& model)
{
	const auto& projectPaths = Project::get().getPath();
	auto& assetManager = Application::get().getAssetManager();

	const auto srcBaseDir = srcPath.parent_path();

	// Import all materials.
	std::unordered_map<int32, AssetSaveInfo> importedMaterials;

	const auto materialFolderPath = savePath / "materials";
	std::filesystem::create_directory(materialFolderPath);
	const auto& meta = GLTFMaterialAsset::kAssetTypeMeta;
	const auto materialStoreRelativePath = buildRelativePath(Project::get().getPath().assetPath.u16(), materialFolderPath);
	int32 index = -1;
	for (auto index = 0; index < model.materials.size(); index ++)
	{
		const auto& material = model.materials[index];

		const std::string materialName =
			material.name.empty() ? std::format("Default_{}", index) : material.name;

		const auto name = utf8::utf8to16(materialName) + utf8::utf8to16(meta.suffix);

		AssetSaveInfo saveInfo(name, materialStoreRelativePath);
		auto materialPtr = assetManager.createAsset<GLTFMaterialAsset>(saveInfo, true);
		materialPtr->markDirty();

		auto assignGLTFTexture = [&](GLTFTextureInfo& info, auto& b)
		{
			if (b.index == -1) { return; }

			tinygltf::Texture texture = model.textures.at(b.index);
			tinygltf::Sampler sampler = model.samplers.at(texture.sampler);

			info.image = importedImageMap.at(texture.source);

			info.textureCoord = b.texCoord;


			info.sampler.minFilter = (sampler.minFilter == -1) ? GLTFSampler::EMinMagFilter::NEAREST : GLTFSampler::EMinMagFilter(sampler.minFilter);
			info.sampler.magFilter = (sampler.magFilter == -1) ? GLTFSampler::EMinMagFilter::NEAREST : GLTFSampler::EMinMagFilter(sampler.magFilter);
			info.sampler.wrapS = (sampler.wrapS == -1) ? GLTFSampler::EWrap::REPEAT : GLTFSampler::EWrap(sampler.wrapS);
			info.sampler.wrapT = (sampler.wrapT == -1) ? GLTFSampler::EWrap::REPEAT : GLTFSampler::EWrap(sampler.wrapT);
		};

		auto& pbr = material.pbrMetallicRoughness;

		materialPtr->baseColorFactor.x = pbr.baseColorFactor[0];
		materialPtr->baseColorFactor.y = pbr.baseColorFactor[1];
		materialPtr->baseColorFactor.z = pbr.baseColorFactor[2];
		materialPtr->baseColorFactor.w = pbr.baseColorFactor[3];

		assignGLTFTexture(materialPtr->baseColorTexture, pbr.baseColorTexture);

		materialPtr->metallicFactor = pbr.metallicFactor;
		materialPtr->roughnessFactor = pbr.roughnessFactor;
		assignGLTFTexture(materialPtr->metallicRoughnessTexture, pbr.metallicRoughnessTexture);

		assignGLTFTexture(materialPtr->emissiveTexture, material.emissiveTexture);
		materialPtr->emissiveFactor.x = material.emissiveFactor[0];
		materialPtr->emissiveFactor.y = material.emissiveFactor[1];
		materialPtr->emissiveFactor.z = material.emissiveFactor[2];

		if (material.alphaMode == "OPAQUE") { materialPtr->alphaMode = EAlphaMode::Opaque; }
		else if (material.alphaMode == "MASK") { materialPtr->alphaMode = EAlphaMode::Mask; }
		else { materialPtr->alphaMode = EAlphaMode::Blend; check(material.alphaMode == "BLEND"); }

		materialPtr->alphaCoutoff = material.alphaCutoff;
		materialPtr->bDoubleSided = material.doubleSided;

		assignGLTFTexture(materialPtr->normalTexture, material.normalTexture);
		materialPtr->normalTextureScale = (float)material.normalTexture.scale;

				
		materialPtr->bExistOcclusion = material.occlusionTexture.index != -1;
		if (materialPtr->bExistOcclusion && pbr.metallicRoughnessTexture.index > -1)
		{
			check(importedImageMap.at(material.occlusionTexture.index) == importedImageMap.at(pbr.metallicRoughnessTexture.index));
		}
		materialPtr->occlusionTextureStrength = (float)material.occlusionTexture.strength;

		if (!materialPtr->save())
		{
			LOG_ERROR("Failed to save material asset {}.", materialPtr->getName().u8());
		}
		else
		{
			importedMaterials[index] = materialPtr->getSaveInfo();
		}
	}

	return std::move(importedMaterials);
}

GLTFMaterialAssetRef chord::tryLoadGLTFMaterialAsset(const std::filesystem::path& path, bool bThreadSafe)
{
	return Application::get().getAssetManager().getOrLoadAsset<GLTFMaterialAsset>(path, bThreadSafe);
}