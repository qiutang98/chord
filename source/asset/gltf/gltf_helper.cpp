#include <nlohmann/json.hpp>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <stb/stb_image_resize.h>
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define TINYGLTF_USE_CPP14
#define TINYGLTF_NO_INCLUDE_JSON 
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#include <tinygltf/tiny_gltf.h>

#include <asset/gltf/gltf_helper.h>
#include <asset/gltf/gltf.h>
#include <asset/asset_common.h>

#include <asset/asset.h>
#include <asset/texture/texture.h>
#include <asset/texture/helper.h>
#include <graphics/helper.h>
#include <graphics/uploader.h>
#include <application/application.h>


#include <ui/ui_helper.h>
#include <asset/texture/texture.h>
#include <project.h>

#include <asset/serialize.h>

#include <asset/xatlas.h>
#include <asset/mikktspace.h>

#include <asset/meshoptimizer/meshoptimizer.h>
#include <renderer/gpu_scene.h>
#include <utils/thread.h>
#include <shader/gltf.h>
#include <utils/cityhash.h>

#include <asset/nanite_builder.h>

namespace chord
{
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

	const std::string& getGLTFExtension(EKHRGLTFExtension ext)
	{
		return kSupportedGLTFExtension[(int)ext];
	}

	const bool isGLTFExtensionSupported(const std::string& name)
	{
		for (const auto& ext : kSupportedGLTFExtension)
		{
			if(name == ext) { return true; }
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

	void uiDrawImportConfig(GLTFAssetImportConfigRef config)
	{
		ImGui::Checkbox("##SmoothNormal", &config->bGenerateSmoothNormal); ImGui::SameLine(); ImGui::Text("Generate Smooth Normal");

		ImGui::DragFloat("Meshlet ConeWeight", &config->meshletConeWeight, 0.1f, 0.0f, 1.0f);

		ImGui::Separator();
		ImGui::Checkbox("##GenerateLOD",  &config->bGenerateLOD); ImGui::SameLine(); ImGui::Text("Generate LOD");
		ImGui::DragFloat("LOD Base",      &config->lodBase, 1.0f, 1.0f, 20.0f);
		ImGui::DragFloat("LOD Step",      &config->lodStep, 0.1f, 1.0f, 2.0f);
		ImGui::DragFloat("LOD ScreenPercentage Scale", &config->lodScreenPercentageScale, 0.1f, 0.1f, 1.0f);

		ImGui::DragFloat("Reduce Factor", &config->lodStepReduceFactor, 0.01f, 0.1f, 1.0f);
		ImGui::DragFloat("Target Error",  &config->lodTargetError, 1e-4f, 0.0001f, 0.02f);
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

		// Check support ext state.
		for (const auto& ext : model.extensionsRequired)
		{
			if (!isGLTFExtensionSupported(ext))
			{
				LOG_ERROR("No supported ext '{0}' used in gltf model '{1}'.", ext, utf8::utf16to8(srcPath.u16string()));
			}
			else
			{
				LOG_TRACE("GLTF model '{1}' using extension '{0}'...", ext, utf8::utf16to8(srcPath.u16string()));
			}
		}

		// Import all images in gltf.
		std::unordered_map<int32, AssetSaveInfo> importedImages;
		{
			const auto imageFolderPath = savePath / "images";
			std::filesystem::create_directory(imageFolderPath);

			// Collected srgb by material usage.
			std::set<int32> srgbImagesMap;
			std::map<int32, float> alphaCoverageImagesMap;

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
					imageChannelUsageMap[metallicRoughnessImageIndex].rgba.g = true;
					imageChannelUsageMap[metallicRoughnessImageIndex].rgba.b = true;
				}

				if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialSpecular)))
				{
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialSpecular)];

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
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialAnisotropy)];
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
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialClearCoat)];

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
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialSheen)];
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

				if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialTransmission)))
				{
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialTransmission)];
					int32 texture;
					getTexId(ext, "transmissionTexture", texture);
					int32 imageIndex = model.textures.at(texture).source;

					imageChannelUsageMap[imageIndex].rgba.r = true;
				}

				if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialVolume)))
				{
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialVolume)];
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
								detail.srcImageChannel  = 0;
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
						ImageLdr2D ldr { };
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
					check(detail.srcImageChannel  <= 3 && detail.srcImageChannel  >= 0);

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

					std::vector<uint8> sizeFitData { };
					if (srcImage.width != destImage.width || srcImage.height != destImage.height)
					{
						sizeFitData.resize(destImage.width* destImage.height * 4);
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
		}

		// Import all materials.
		std::unordered_map<int32, AssetSaveInfo> importedMaterials;
		{
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

					info.image = importedImages.at(texture.source);

					info.textureCoord = b.texCoord;


					info.sampler.minFilter = (sampler.minFilter == -1) ? GLTFSampler::EMinMagFilter::NEAREST : GLTFSampler::EMinMagFilter(info.sampler.minFilter);
					info.sampler.magFilter = (sampler.magFilter == -1) ? GLTFSampler::EMinMagFilter::NEAREST : GLTFSampler::EMinMagFilter(info.sampler.magFilter);
					info.sampler.wrapS = (sampler.wrapS == -1) ? GLTFSampler::EWrap::REPEAT : GLTFSampler::EWrap(info.sampler.wrapS);
					info.sampler.wrapT = (sampler.wrapT == -1) ? GLTFSampler::EWrap::REPEAT : GLTFSampler::EWrap(info.sampler.wrapT);
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
					check(importedImages.at(material.occlusionTexture.index) == importedImages.at(pbr.metallicRoughnessTexture.index));
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
		}

		GLTFAssetRef gltfPtr;
		{
			// Create asset gltf.
			AssetSaveInfo saveInfo = config->getSaveInfo(meta.suffix);
			gltfPtr = assetManager.createAsset<GLTFAsset>(saveInfo, true);
		}
		gltfPtr->markDirty();

		// GLTF scene graph to world node.
		GLTFBinary gltfBin{};
		{
			gltfPtr->m_defaultScene = model.defaultScene;

			for (const auto& scene : model.scenes)
			{
				GLTFScene gltfScene;
				gltfScene.name = scene.name;
				gltfScene.nodes = scene.nodes;

				gltfPtr->m_scenes.push_back(std::move(gltfScene));
			}

			for (const auto& node : model.nodes)
			{
				GLTFNode gltfNode;
				gltfNode.name = node.name;
				gltfNode.childrenIds = node.children;
				gltfNode.mesh = node.mesh;

				gltfNode.localMatrix = math::dmat4(1.0);

				// TRS style.
				if (node.translation.size() == 3)
				{
					gltfNode.localMatrix = math::translate(gltfNode.localMatrix, math::make_vec3(node.translation.data()));
				}
				if (node.rotation.size() == 4)
				{
					math::dquat q = math::make_quat(node.rotation.data());
					gltfNode.localMatrix *= math::dmat4(q);
				}
				if (node.scale.size() == 3)
				{
					gltfNode.localMatrix = math::scale(gltfNode.localMatrix, glm::make_vec3(node.scale.data()));
				}

				// If composite matrix exist just use it.
				if (node.matrix.size() == 16) 
				{
					gltfNode.localMatrix = glm::make_mat4x4(node.matrix.data());
				};

				gltfPtr->m_nodes.push_back(std::move(gltfNode));
			}
			
			// Load all mesh data.
			std::unordered_map<std::string, GLTFPrimitive> cachePrimMesh;
			auto processMesh = [&](GLTFPrimitive& primitiveMesh, const tinygltf::Model& model, const tinygltf::Primitive& mesh, const std::string& name, bool bGenerateSmoothNormal)
			{
				// Only triangles are supported
				// 0:point, 1:lines, 2:line_loop, 3:line_strip, 4:triangles, 5:triangle_strip, 6:triangle_fan
				if (mesh.mode != 4)
				{
					LOG_ERROR("Current GLTF mesh '{}' no triangle mesh, skip...", name);
					return;
				}

				bool bPrimitiveCache = false;
				std::string key;
				{
					// Create a key made of the attributes, to see if the primitive was already
					// processed. If it is, we will re-use the cache, but allow the material and
					// indices to be different.

					std::stringstream o;
					for (auto& a : mesh.attributes)
					{
						o << a.first << a.second;
					}
					key = o.str();

					// Found a cache - will not need to append vertex
					auto it = cachePrimMesh.find(key);
					if (it != cachePrimMesh.end())
					{
						bPrimitiveCache = true;

						// Copy value.
						primitiveMesh = it->second;

						LOG_TRACE("Primitive '{0}' cache found same format which created by primitive '{1}', we reuse cache one to save memory.", name, primitiveMesh.name);
					}
				}

				// Name and material can be special.
				primitiveMesh.name = name;
				if (mesh.material > -1)
				{
					primitiveMesh.material = importedMaterials[mesh.material];
				}

				if (!bPrimitiveCache)
				{
					if (!mesh.attributes.contains("POSITION"))
					{
						LOG_ERROR("GLTF file is unvalid: a primitive without POSITION attribute, skip...");
						return;
					}

					primitiveMesh.lodStep = config->lodStep;
					primitiveMesh.lodBase = config->lodBase;
					primitiveMesh.lodScreenPercentageScale = config->lodScreenPercentageScale;

					std::vector<uint32> tempIndices;
					std::vector<math::vec3> tempPositions;
					std::vector<math::vec3> tempNormals;
					std::vector<math::vec2> tempUv0s;
					math::vec3 tempPosMin;
					math::vec3 tempPosMax;
					math::vec3 tempPosAvg;

					if (mesh.indices > -1)
					{
						const tinygltf::Accessor& indexAccessor = model.accessors[mesh.indices];
						const tinygltf::BufferView& bufferView = model.bufferViews[indexAccessor.bufferView];

						tempIndices.resize(indexAccessor.count);
						auto insertIndices = [&]<typename T>()
						{
							const auto* buf = reinterpret_cast<const T*>(&model.buffers[bufferView.buffer].data[indexAccessor.byteOffset + bufferView.byteOffset]);
							for (auto index = 0; index < indexAccessor.count; index++)
							{
								tempIndices[index] = buf[index];
							}
						};
						switch (indexAccessor.componentType)
						{
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: { insertIndices.operator()<uint32>(); break; }
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: { insertIndices.operator()<uint16>(); break; }
						case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: { insertIndices.operator()<uint8>(); break; }
						default: LOG_ERROR("Index component type %i not supported!\n", indexAccessor.componentType); return;
						}
					}
					else
					{
						LOG_TRACE("No INDICES found in mesh '{}', generating...", primitiveMesh.name);

						const auto& accessor = model.accessors[mesh.attributes.find("POSITION")->second];
						tempIndices.resize(accessor.count);
						for (auto i = 0; i < accessor.count; i++)
						{
							tempIndices[i] = i;
						}
					}

					// Position.
					{
						const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("POSITION")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

						const float* posBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						math::vec3 positionMin = math::vec3( std::numeric_limits<float>::max());
						math::vec3 positionMax = math::vec3(-std::numeric_limits<float>::max());

						// Summary of position, use double to keep precision.
						math::dvec3 positionSum = math::dvec3(0.0);
						tempPositions.resize(accessor.count);
						for (auto index = 0; index < accessor.count; index++)
						{
							tempPositions[index] = { posBuffer[0], posBuffer[1], posBuffer[2] };
							posBuffer += 3;

							positionMin = math::min(positionMin, tempPositions[index]);
							positionMax = math::max(positionMax, tempPositions[index]);

							positionSum += tempPositions[index];
						}

						tempPosMax = positionMax;
						tempPosMin = positionMin;

						// Position average.
						tempPosAvg = positionSum / double(accessor.count);
					}

					// Normal.
					if (mesh.attributes.contains("NORMAL"))
					{
						const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("NORMAL")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

						const float* normalBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						tempNormals.resize(accessor.count);
						for (auto index = 0; index < accessor.count; index++)
						{
							tempNormals[index] = { normalBuffer[0], normalBuffer[1], normalBuffer[2] };
							normalBuffer += 3;
						}
					}
					else
					{
						LOG_TRACE("No NORMAL found in mesh '{}', generating...", primitiveMesh.name);

						tempNormals.resize(tempPositions.size());
						for (auto i = 0; i < tempIndices.size(); i += 3)
						{
							uint32 ind0 = tempIndices[i + 0];
							uint32 ind1 = tempIndices[i + 1];
							uint32 ind2 = tempIndices[i + 2];

							const auto& pos0 = tempPositions[ind0];
							const auto& pos1 = tempPositions[ind1];
							const auto& pos2 = tempPositions[ind2];

							const auto v1 = math::normalize(pos1 - pos0);
							const auto v2 = math::normalize(pos2 - pos0);
							const auto n  = math::normalize(glm::cross(v1, v2));

							tempNormals[ind0] = n;
							tempNormals[ind1] = n;
							tempNormals[ind2] = n;
						}
					}

					// Texture Coordinate 0.
					const bool bExistTextureCoord0 = mesh.attributes.contains("TEXCOORD_0");
					if (bExistTextureCoord0)
					{
						const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("TEXCOORD_0")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

						const float* textureCoord0Buffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						tempUv0s.resize(accessor.count);
						for (auto index = 0; index < accessor.count; index++)
						{
							tempUv0s[index] = { textureCoord0Buffer[0], textureCoord0Buffer[1] };
							textureCoord0Buffer += 2;
						}
					}
					else
					{
						LOG_TRACE("No TEXCOORD_0 found in mesh '{}', generating...", primitiveMesh.name);

						// NOTE: UV split will copy vertex in seam, so vertex count increment.
						auto* singleMeshAtlas = xatlas::Create();

						xatlas::MeshDecl meshDecl;
						meshDecl.vertexCount          = tempPositions.size();
						meshDecl.vertexPositionData   = tempPositions.data();
						meshDecl.vertexPositionStride = sizeof(tempPositions[0]);
						meshDecl.vertexNormalData     = tempNormals.data();
						meshDecl.vertexNormalStride   = sizeof(tempNormals[0]);
						meshDecl.indexCount           = tempIndices.size();
						meshDecl.indexData            = tempIndices.data();
						meshDecl.indexOffset          = 0;
						meshDecl.indexFormat          = xatlas::IndexFormat::UInt32;

						xatlas::AddMeshError error = xatlas::AddMesh(singleMeshAtlas, meshDecl, 1);
						if (error != xatlas::AddMeshError::Success) 
						{
							xatlas::Destroy(singleMeshAtlas);
							LOG_ERROR("Error adding mesh '{}' for uv generation.", primitiveMesh.name);
							return;
						}

						xatlas::Generate(singleMeshAtlas);
						auto& generatedMesh = singleMeshAtlas->meshes[0];

						std::vector<math::vec3> newPositions(generatedMesh.vertexCount);
						std::vector<math::vec3> newNormals(generatedMesh.vertexCount);
						std::vector<math::vec2> newUv0s(generatedMesh.vertexCount);

						float invW = 1.0f / float(singleMeshAtlas->width);
						float invH = 1.0f / float(singleMeshAtlas->height);

						math::dvec3 newAvgPos = math::dvec3{ 0.0 };
						for (uint32 v = 0; v < generatedMesh.vertexCount; v++)
						{
							const xatlas::Vertex& vertex = generatedMesh.vertexArray[v];
							uint32 originalVertex = vertex.xref * 3;

							newPositions[v] = tempPositions[originalVertex];
							newAvgPos += newPositions[v];

							newNormals[v]   = tempNormals[originalVertex];
							newUv0s[v]      = { vertex.uv[0] * invW, vertex.uv[1] * invH };
						}

						newAvgPos /= double(generatedMesh.vertexCount);

						std::vector<uint32> newIndices(generatedMesh.indexCount);
						for (uint32 i = 0; i < generatedMesh.indexCount; i ++)
						{
							newIndices[i] = generatedMesh.indexArray[i];
						}

						tempPositions = std::move(newPositions);
						tempNormals   = std::move(newNormals);
						tempUv0s      = std::move(newUv0s);
						tempIndices   = std::move(newIndices);

						// NOTE: Only average position need recompute, min and max pos unchange.
						tempPosAvg    = std::move(newAvgPos);

						// NOTE: Tangent auto recompute when uv0 input unvalid.
						xatlas::Destroy(singleMeshAtlas);	
					}


					// Mesh lod and meshlet generation.
					uint32 lod0FirstIndex = gltfBin.primitiveData.indices.size();
					uint32 lod0IndexCount = tempIndices.size();

					meshopt_optimizeVertexCache(tempIndices.data(), tempIndices.data(), tempIndices.size(), tempPositions.size());
						
					auto computeMeshlet = [&](const std::vector<math::vec3>& positions, const std::vector<uint32>& indices, std::vector<uint32>& remapIndices)
					{
						remapIndices = {};
						remapIndices.reserve(indices.size());

						std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(indices.size(), kMeshletMaxVertices, kMeshletMaxTriangles));
						std::vector<uint32> meshletVertices(meshlets.size() * kMeshletMaxVertices);
						std::vector<uint8> meshletTriangles(meshlets.size() * kMeshletMaxTriangles * 3);

						meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), indices.data(), indices.size(), &positions[0].x, positions.size(), sizeof(positions[0]), kMeshletMaxVertices, kMeshletMaxTriangles, config->meshletConeWeight));

						for (const auto& meshlet : meshlets)
						{
							meshopt_optimizeMeshlet(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);

							meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, &positions[0].x, positions.size(), sizeof(positions[0]));
							GLTFMeshlet m = { };

							// Triangle count and first index.
							m.data.triangleCount = meshlet.triangle_count;
							m.data.firstIndex    = remapIndices.size();

							// Cone info.
							m.data.coneCutOff = bounds.cone_cutoff;
							m.data.coneAxis.x = bounds.cone_axis[0];
							m.data.coneAxis.y = bounds.cone_axis[1];
							m.data.coneAxis.z = bounds.cone_axis[2];
							m.data.coneApex.x = bounds.cone_apex[0];
							m.data.coneApex.y = bounds.cone_apex[1];
							m.data.coneApex.z = bounds.cone_apex[2];
							
							// Position info.
							math::vec3 posMin = math::vec3( FLT_MAX);
							math::vec3 posMax = math::vec3(-FLT_MAX);

							// Loop all triangle, also fill new indices.
							for (uint32 triangleId = 0; triangleId < meshlet.triangle_count; triangleId++)
							{
								uint8 id0 = meshletTriangles[meshlet.triangle_offset + triangleId * 3 + 0];
								uint8 id1 = meshletTriangles[meshlet.triangle_offset + triangleId * 3 + 1];
								uint8 id2 = meshletTriangles[meshlet.triangle_offset + triangleId * 3 + 2];

								uint32 index0 = meshletVertices[meshlet.vertex_offset + id0];
								uint32 index1 = meshletVertices[meshlet.vertex_offset + id1];
								uint32 index2 = meshletVertices[meshlet.vertex_offset + id2];

								remapIndices.push_back(index0);
								remapIndices.push_back(index1);
								remapIndices.push_back(index2);

								posMax = math::max(posMax, positions[index0]);
								posMax = math::max(posMax, positions[index1]);
								posMax = math::max(posMax, positions[index2]);

								posMin = math::min(posMin, positions[index0]);
								posMin = math::min(posMin, positions[index1]);
								posMin = math::min(posMin, positions[index2]);
							}

							m.data.posMin = posMin;
							m.data.posMax = posMax;
							gltfBin.primitiveData.meshlets.push_back(m);
						}

						return meshlets.size();
					};

					std::vector<uint32> lodIndices = std::move(tempIndices);
					while (primitiveMesh.lods.size() < kMaxGLTFLodCount)
					{
						primitiveMesh.lods.push_back({});
						GLTFPrimitiveLOD& lod = primitiveMesh.lods.back();

						lod.data.firstIndex   = gltfBin.primitiveData.indices.size();
						lod.data.indexCount   = static_cast<uint32>(lodIndices.size());
						lod.data.firstMeshlet = gltfBin.primitiveData.meshlets.size();

						std::vector<uint32> remapIndices;
						lod.data.meshletCount = computeMeshlet(tempPositions, lodIndices, remapIndices);
						check(remapIndices.size() == lodIndices.size());

						// Insert to bin data: use remap indices.
						gltfBin.primitiveData.indices.insert(gltfBin.primitiveData.indices.end(), remapIndices.begin(), remapIndices.end());

						if (primitiveMesh.lods.size() < kMaxGLTFLodCount)
						{
							uint32 nextLodIndicesTarget = uint32(double(lodIndices.size()) * double(config->lodStepReduceFactor));
							float errorResult;
							uint32 nextIndices = meshopt_simplify(
								lodIndices.data(), 
								lodIndices.data(), 
								lodIndices.size(), 
								&tempPositions[0].x, 
								tempPositions.size(), 
								sizeof(tempPositions[0]), 
								nextLodIndicesTarget, 
								config->lodTargetError,
								0,
								&errorResult);
								
							check(nextIndices <= lodIndices.size());
							if (nextIndices == lodIndices.size())
							{
								// Reach error bound, pre-return.
								LOG_TRACE("Mesh '{1}' lod pre-break at lod level {0}.", primitiveMesh.lods.size(), primitiveMesh.name);
								break;
							}
							else
							{
								LOG_TRACE("Mesh '{1}' lod#{0} generated, current target error is {2}, and the error result is {3}.",
									primitiveMesh.lods.size(), primitiveMesh.name, config->lodTargetError, errorResult);
							}

							lodIndices.resize(nextIndices);
							meshopt_optimizeVertexCache(lodIndices.data(), lodIndices.data(), lodIndices.size(), tempPositions.size());
						}
					}

					// Get vertex offset.
					primitiveMesh.vertexOffset = gltfBin.primitiveData.positions.size();
					check(primitiveMesh.vertexOffset == gltfBin.primitiveData.normals.size());
					check(primitiveMesh.vertexOffset == gltfBin.primitiveData.texcoords0.size());
					check(primitiveMesh.vertexOffset == gltfBin.primitiveData.tangents.size());

					// Optional attribute offset.
					primitiveMesh.colors0Offset       = gltfBin.primitiveData.colors0.size();
					primitiveMesh.textureCoord1Offset = gltfBin.primitiveData.texcoords1.size();
					primitiveMesh.smoothNormalOffset  = gltfBin.primitiveData.smoothNormals.size();

					// 
					primitiveMesh.vertexCount = tempPositions.size();

					// Position min, max and average.
					primitiveMesh.posMin = tempPosMin; // NOTE: UV layout copy positions.
					primitiveMesh.posMax = tempPosMax;
					primitiveMesh.posAverage = tempPosAvg;

					// Insert back to binary data.
					gltfBin.primitiveData.texcoords0.insert(gltfBin.primitiveData.texcoords0.end(), tempUv0s.begin(), tempUv0s.end());
					gltfBin.primitiveData.positions.insert(gltfBin.primitiveData.positions.end(), tempPositions.begin(), tempPositions.end());
					gltfBin.primitiveData.normals.insert(gltfBin.primitiveData.normals.end(), tempNormals.begin(), tempNormals.end());


					// Tangent.
					if (bExistTextureCoord0 && mesh.attributes.contains("TANGENT"))
					{
						// Tangent already exist so just import.
						const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("TANGENT")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

						const float* tangentBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						for (auto index = 0; index < accessor.count; index++)
						{
							gltfBin.primitiveData.tangents.push_back({ tangentBuffer[0], tangentBuffer[1], tangentBuffer[2], tangentBuffer[3] });
							tangentBuffer += 4;
						}
					}
					else
					{
						if (!bExistTextureCoord0)
						{
							LOG_TRACE("Mesh uv0 auto layout when import '{}', src tangent is unvalid, generating mikktspace...", primitiveMesh.name);
						}
						else
						{
							LOG_TRACE("No tangent found in mesh '{}', generating mikktspace...", primitiveMesh.name);
						}

						struct MikkTSpaceContext
						{
							GLTFPrimitive* mesh;
							GLTFBinary::PrimitiveDatas* data;
							std::vector<math::vec4> tangents;
						} computeCtx;

						computeCtx.mesh = &primitiveMesh;
						computeCtx.data = &gltfBin.primitiveData;
						computeCtx.tangents.resize(primitiveMesh.vertexCount);

						SMikkTSpaceContext ctx{};
						SMikkTSpaceInterface ctxI{};
						ctx.m_pInterface = &ctxI;
						ctx.m_pUserData = &computeCtx;
						ctx.m_pInterface->m_getNumFaces = [](const SMikkTSpaceContext* pContext) -> int
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							return ctx->mesh->lods[0].data.indexCount / 3;
						};
						ctx.m_pInterface->m_getNumVerticesOfFace = [](const SMikkTSpaceContext* pContext, const int iFace) -> int
						{
							return 3;
						};
						ctx.m_pInterface->m_getPosition = [](const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert) -> void
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							uint32 id = ctx->data->indices[iFace * 3 + iVert + ctx->mesh->lods[0].data.firstIndex] + ctx->mesh->vertexOffset;
							fvPosOut[0] = ctx->data->positions[id].x;
							fvPosOut[1] = ctx->data->positions[id].y;
							fvPosOut[2] = ctx->data->positions[id].z;
						};
						ctx.m_pInterface->m_getNormal = [](const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert) -> void
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							uint32 id = ctx->data->indices[iFace * 3 + iVert + ctx->mesh->lods[0].data.firstIndex] + ctx->mesh->vertexOffset;

							fvNormOut[0] = ctx->data->normals[id].x;
							fvNormOut[1] = ctx->data->normals[id].y;
							fvNormOut[2] = ctx->data->normals[id].z;
						};
						ctx.m_pInterface->m_getTexCoord = [](const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert) -> void
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							uint32 id = ctx->data->indices[iFace * 3 + iVert + ctx->mesh->lods[0].data.firstIndex] + ctx->mesh->vertexOffset;

							fvTexcOut[0] = ctx->data->texcoords0[id].x;
							fvTexcOut[1] = ctx->data->texcoords0[id].y;
						};
						ctx.m_pInterface->m_setTSpaceBasic = [](const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							uint32 id = ctx->data->indices[iFace * 3 + iVert + ctx->mesh->lods[0].data.firstIndex];

							ctx->tangents[id].x = fvTangent[0];
							ctx->tangents[id].y = fvTangent[1];
							ctx->tangents[id].z = fvTangent[2];
							ctx->tangents[id].w = fSign;
						};

						if (!genTangSpaceDefault(&ctx))
						{
							LOG_ERROR("Mesh '{}' mikktspace generate error, skip.", primitiveMesh.name);
							return;
						}

						gltfBin.primitiveData.tangents.insert(gltfBin.primitiveData.tangents.end(), computeCtx.tangents.begin(), computeCtx.tangents.end());
					}

					// Smooth normals.
					if (bGenerateSmoothNormal)
					{
						primitiveMesh.bSmoothNormalExist = true;

						std::vector<math::vec3> newSmoothNormals(primitiveMesh.vertexCount);
						for (auto i = 0; i < lod0IndexCount; i += 3)
						{
							uint32 ind0 = gltfBin.primitiveData.indices[lod0FirstIndex + i + 0];
							uint32 ind1 = gltfBin.primitiveData.indices[lod0FirstIndex + i + 1];
							uint32 ind2 = gltfBin.primitiveData.indices[lod0FirstIndex + i + 2];

							const auto n0 = gltfBin.primitiveData.normals[ind0];
							const auto n1 = gltfBin.primitiveData.normals[ind1];
							const auto n2 = gltfBin.primitiveData.normals[ind2];

							newSmoothNormals[ind0] += n0;
							newSmoothNormals[ind1] += n1;
							newSmoothNormals[ind2] += n2;
						}

						for (auto& n : newSmoothNormals)
						{
							n = math::normalize(n);
						}
						gltfBin.primitiveData.smoothNormals.insert(gltfBin.primitiveData.smoothNormals.end(), newSmoothNormals.begin(), newSmoothNormals.end());
					}

					/////////////////////////////
					// Optional attributes import.
					// 
					
					// Texture Coordinate 1.
					if (mesh.attributes.contains("TEXCOORD_1"))
					{
						primitiveMesh.bTextureCoord1Exist = true;
						const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("TEXCOORD_1")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

						const float* textureCoord1Buffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						for (auto index = 0; index < accessor.count; index++)
						{
							gltfBin.primitiveData.texcoords1.push_back({ textureCoord1Buffer[0], textureCoord1Buffer[1] });
							textureCoord1Buffer += 2;
						}
					}

					// Color 0.
					if (mesh.attributes.contains("COLOR_0"))
					{
						primitiveMesh.bColor0Exist = true;
						const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("COLOR_0")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

						const float* colorBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						for (auto index = 0; index < accessor.count; index++)
						{
							gltfBin.primitiveData.colors0.push_back({ colorBuffer[0], colorBuffer[1], colorBuffer[2], colorBuffer[3]});
							colorBuffer += 4;
						}
					}

					cachePrimMesh[key] = primitiveMesh;
				}
			};

			for (const auto& mesh : model.meshes)
			{
				GLTFMesh gltfMesh;

				gltfMesh.name = mesh.name;

				for (const auto& primitive : mesh.primitives)
				{
					GLTFPrimitive gltfPrimitive;
					processMesh(gltfPrimitive, model, primitive, mesh.name, config->bGenerateSmoothNormal);

					// Prepare gltf mesh.
					gltfMesh.primitives.push_back(gltfPrimitive);
				}

				gltfPtr->m_meshes.push_back(std::move(gltfMesh));
			}
		}

		gltfPtr->m_gltfBinSize = gltfBin.primitiveData.size();
		saveAsset(gltfBin, ECompressionMode::Lz4, gltfPtr->getBinPath(), false);

		return gltfPtr->save();
	}

	GLTFMaterialAssetRef tryLoadGLTFMaterialAsset(const std::filesystem::path& path, bool bThreadSafe)
	{
		return Application::get().getAssetManager().getOrLoadAsset<GLTFMaterialAsset>(path, bThreadSafe);
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

	AssetTypeMeta GLTFMaterialAsset::createTypeMeta()
	{
		AssetTypeMeta result;
		// 
		result.name = "GLTFMaterial";
		result.icon = ICON_FA_FEATHER_POINTED;
		result.decoratedName = std::string("  ") + ICON_FA_FEATHER_POINTED + "    GLTFMaterial";

		//
		result.suffix = ".assetgltfmaterial";
		result.importConfig.bImportable = false;

		return result;
	}

	GPUGLTFPrimitiveAsset::GPUGLTFPrimitiveAsset(const std::string& inName, std::shared_ptr<GLTFAsset> asset)
		: IUploadAsset(nullptr)
		, m_name(inName)
		, m_gltfAssetWeak(asset)
	{
		
	}

	GPUGLTFPrimitiveAsset::~GPUGLTFPrimitiveAsset()
	{
		freeGPUScene();
	}

	ComponentBuffer::ComponentBuffer(
		const std::string& name, 
		VkBufferUsageFlags flags, 
		VmaAllocationCreateFlags vmaFlags,
		uint32 stripe,
		uint32 num)
	{
		this->elementNum = num;
		this->stripe = stripe;

		VkBufferCreateInfo ci{ };
		ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		ci.size = stripe * num;
		ci.usage = flags;
		ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo vmaCI{};
		vmaCI.usage = VMA_MEMORY_USAGE_AUTO;
		vmaCI.flags = vmaFlags;

		// Buffer handle.
		this->buffer = std::make_shared<graphics::GPUBuffer>(name, ci, vmaCI);
		
		// Register bindless.
		this->bindless = graphics::getContext().getBindlessManger().registerStorageBuffer(*this->buffer, 0, this->buffer->getSize());
	}

	ComponentBuffer::~ComponentBuffer()
	{
		if (bindless.isValid())
		{
			const bool bReleasing = (Application::get().getRuntimePeriod() == ERuntimePeriod::Releasing);
			graphics::GPUBufferRef fallback = bReleasing ? nullptr : graphics::getContext().getDummySSBO();

			// Free buffer bindless index.
			graphics::getContext().getBindlessManger().freeStorageBuffer(bindless, fallback);
		}
	}

	uint32 GPUGLTFPrimitiveAsset::getSize() const
	{
		auto getValidSize = [](const std::unique_ptr<ComponentBuffer>& buffer) -> uint32 
		{ 
			if (buffer != nullptr) 
			{ 
				return buffer->buffer->getSize(); 
			} 
			return 0;
		};

		return 
			  getValidSize(indices) 
			+ getValidSize(positions) 
			+ getValidSize(normals) 
			+ getValidSize(uv0s) 
			+ getValidSize(tangents)
			+ getValidSize(colors)
			+ getValidSize(uv1s)
			+ getValidSize(smoothNormals)
			+ getValidSize(meshlet);
	}

	inline uint64 getPrimitiveDetailHash(uint64 GPUSceneHash, uint32 meshId, uint32 primitiveId)
	{
		uint64 pack = (uint64(meshId) << 32) | primitiveId;
		return hashCombine(pack, GPUSceneHash);
	}

	void GPUGLTFPrimitiveAsset::updateGPUScene()
	{
		check(isInMainThread());
		freeGPUScene();

		auto& gltfPrimitiveAssetPool = Application::get().getGPUScene().getGLTFPrimitiveDataPool();
		m_gpuSceneGLTFPrimitiveAssetId = gltfPrimitiveAssetPool.requireId(GPUSceneHash());

		GLTFPrimitiveDatasBuffer uploadData { };

		#define ASSIGN_DATA(A, B) if(A != nullptr) { uploadData.B = A->bindless.get(); } else { uploadData.B = ~0; }
		{
			ASSIGN_DATA(indices, indicesBuffer);
			ASSIGN_DATA(positions, positionBuffer);
			ASSIGN_DATA(normals, normalBuffer);
			ASSIGN_DATA(uv0s, textureCoord0Buffer);
			ASSIGN_DATA(tangents, tangentBuffer);
			ASSIGN_DATA(colors, color0Buffer);
			ASSIGN_DATA(uv1s, textureCoord1Buffer);
			ASSIGN_DATA(smoothNormals, smoothNormalsBuffer);
			ASSIGN_DATA(meshlet, meshletBuffer);
		}
		#undef ASSIGN_DATA

		{
			std::array<math::uvec4, GPUGLTFPrimitiveAsset::kGPUSceneDataFloat4Count> uploadDatas{};
			memcpy(uploadDatas.data(), &uploadData, sizeof(uploadData));

			gltfPrimitiveAssetPool.updateId(m_gpuSceneGLTFPrimitiveAssetId, uploadDatas);
		}

		//
		auto assetRef = m_gltfAssetWeak.lock();
		m_gpuSceneGLTFPrimitiveDetailAssetId.resize(assetRef->getMeshes().size());

		auto& gltfPrimitiveDetailPool = Application::get().getGPUScene().getGLTFPrimitiveDetailPool();
		for (uint32 meshId = 0; meshId < assetRef->getMeshes().size(); meshId++)
		{
			auto& meshPrimitiveIds = m_gpuSceneGLTFPrimitiveDetailAssetId[meshId];
			const auto& meshInfo = assetRef->getMeshes().at(meshId);
			meshPrimitiveIds.resize(meshInfo.primitives.size());
			for (uint32 primitiveId = 0; primitiveId < meshInfo.primitives.size(); primitiveId++)
			{
				// Require GPU scene id.
				const auto primitiveHash = getPrimitiveDetailHash(GPUSceneHash(), meshId, primitiveId);
				meshPrimitiveIds[primitiveId] = gltfPrimitiveDetailPool.requireId(primitiveHash);

				const auto& primitiveInfo = meshInfo.primitives[primitiveId];

				// Fill upload content.
				GLTFPrimitiveBuffer primitiveBufferData{};
				{
					for (uint32 i = 0; i < kMaxGLTFLodCount; i++)
					{
						GLTFPrimitiveLOD lod{};
						if (i < primitiveInfo.lods.size())
						{
							lod = primitiveInfo.lods[i];
						}

						primitiveBufferData.lods[i] = lod.data;
					}

					primitiveBufferData.posMin = primitiveInfo.posMin;
					primitiveBufferData.primitiveDatasBufferId = m_gpuSceneGLTFPrimitiveAssetId;
					primitiveBufferData.posMax = primitiveInfo.posMax;
					primitiveBufferData.vertexOffset = primitiveInfo.vertexOffset;
					primitiveBufferData.posAverage = primitiveInfo.posAverage;
					primitiveBufferData.vertexCount = primitiveInfo.vertexCount;
					primitiveBufferData.color0Offset = primitiveInfo.colors0Offset;
					primitiveBufferData.smoothNormalOffset = primitiveInfo.smoothNormalOffset;
					primitiveBufferData.textureCoord1Offset = primitiveInfo.textureCoord1Offset;
					primitiveBufferData.lodCount = primitiveInfo.lods.size();
					primitiveBufferData.lodBase = 1.0f / primitiveInfo.lodBase;
					primitiveBufferData.loadStep = 1.0f / math::log2(primitiveInfo.lodStep);
					primitiveBufferData.lodScreenPercentageScale = primitiveInfo.lodScreenPercentageScale;

				}

				std::array<math::uvec4, GPUGLTFPrimitiveAsset::kGPUSceneDetailFloat4Count> uploadDatas{};
				memcpy(uploadDatas.data(), &primitiveBufferData, sizeof(primitiveBufferData));

				// Updata to GPUScene.
				gltfPrimitiveDetailPool.updateId(meshPrimitiveIds[primitiveId], uploadDatas);
			}
		}

		enqueueGPUSceneUpdate();
	}

	void GPUGLTFPrimitiveAsset::freeGPUScene()
	{
		check(isInMainThread());

		if (m_gpuSceneGLTFPrimitiveAssetId != -1)
		{
			auto& gltfPrimitiveAssetPool = Application::get().getGPUScene().getGLTFPrimitiveDataPool();

			uint32 id = gltfPrimitiveAssetPool.free(GPUSceneHash());
			check(id == m_gpuSceneGLTFPrimitiveAssetId);

			m_gpuSceneGLTFPrimitiveAssetId = -1;
		}

		auto& gltfPrimitiveDetailPool = Application::get().getGPUScene().getGLTFPrimitiveDetailPool();
		for (uint32 meshId = 0; meshId < m_gpuSceneGLTFPrimitiveDetailAssetId.size(); meshId++)
		{
			const auto& primitiveInfos = m_gpuSceneGLTFPrimitiveDetailAssetId[meshId];
			for (uint32 primitiveId = 0; primitiveId < primitiveInfos.size(); primitiveId++)
			{
				const auto hash = getPrimitiveDetailHash(GPUSceneHash(), meshId, primitiveId);

				uint32 freeId = gltfPrimitiveDetailPool.free(hash);
				check(freeId == primitiveInfos[primitiveId]);
			}
		}
		m_gpuSceneGLTFPrimitiveDetailAssetId.clear();
	}

	uint32 GPUGLTFPrimitiveAsset::getGPUSceneId() const
	{
		return m_gpuSceneGLTFPrimitiveAssetId;
	}

	uint32 GPUGLTFPrimitiveAsset::getGPUScenePrimitiveDetailId(uint32 meshId, uint32 primitiveId) const
	{
		return m_gpuSceneGLTFPrimitiveDetailAssetId.at(meshId).at(primitiveId);
	}
}