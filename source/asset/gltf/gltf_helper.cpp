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

	static inline void findUsedMeshes(const tinygltf::Model& model, std::set<uint32>& usedMeshes, int nodeIdx)
	{
		const auto& node = model.nodes[nodeIdx];
		if (node.mesh >= 0)
		{
			usedMeshes.insert(node.mesh);
		}

		for (const auto& child : node.children)
		{
			findUsedMeshes(model, usedMeshes, child);
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
		std::unordered_map<int32, AssetSaveInfo> importedTextures;
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

				if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialSpecular)))
				{
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialSpecular)];

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

				if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialAnisotropy)))
				{
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialAnisotropy)];
					int32 texture;
					getTexId(ext, "anisotropyTexture", texture);

					// Red and green channels represent the anisotropy direction in [-1, 1] tangent, bitangent space, to be rotated by anisotropyRotation. 
					// The blue channel contains strength as [0, 1] to be multiplied by anisotropyStrength.
					// Anisotropy texture use rgb.
					textureChannelUsageMap[texture].rgba.r = true;
					textureChannelUsageMap[texture].rgba.g = true;
					textureChannelUsageMap[texture].rgba.b = true;
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

					textureChannelUsageMap[clearcoatTexture].rgba.r = true;
					textureChannelUsageMap[clearcoatRoughnessTexture].rgba.g = true;

					textureChannelUsageMap[clearcoatNormalTexture].rgba.r = true;
					textureChannelUsageMap[clearcoatNormalTexture].rgba.g = true;

					if (clearcoatTexture == clearcoatNormalTexture || clearcoatRoughnessTexture == clearcoatNormalTexture)
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

				if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialTransmission)))
				{
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialTransmission)];
					int32 texture;
					getTexId(ext, "transmissionTexture", texture);

					textureChannelUsageMap[texture].rgba.r = true;
				}

				if (material.extensions.contains(getGLTFExtension(EKHRGLTFExtension::MaterialVolume)))
				{
					const auto& ext = material.extensions[getGLTFExtension(EKHRGLTFExtension::MaterialVolume)];
					int32 texture;
					getTexId(ext, "thicknessTexture", texture);

					textureChannelUsageMap[texture].rgba.g = true;
				}
			}

			// Now load all images.
			for (int32 imageIndex = 0; imageIndex < model.images.size(); imageIndex++)
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

				AssetSaveInfo saveInfo;
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
					textureAssetImportConfig->storeFilePath = imageFolderPath / std::filesystem::path(imgName).replace_extension();
					textureAssetImportConfig->bSRGB = bSrgb;
					textureAssetImportConfig->bGenerateMipmap = true;
					textureAssetImportConfig->alphaMipmapCutoff = bAlphaCoverage ? alphaCoverageMap[imageIndex] : 1.0f;
					textureAssetImportConfig->format = textureChannelUsageMap[imageIndex].getFormat(b16Bit);

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
					textureAssetImportConfig->storeFilePath = imageFolderPath / uri.filename().replace_extension();
					textureAssetImportConfig->bSRGB = bSrgb;
					textureAssetImportConfig->bGenerateMipmap = true;
					textureAssetImportConfig->alphaMipmapCutoff = bAlphaCoverage ? alphaCoverageMap[imageIndex] : 1.0f;
					textureAssetImportConfig->format = textureChannelUsageMap[imageIndex].getFormat(b16Bit);

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
					importedTextures[imageIndex] = saveInfo;
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

				const auto name = utf8::utf8to16(material.name) + utf8::utf8to16(meta.suffix);

				AssetSaveInfo saveInfo(name, materialStoreRelativePath);
				auto materialPtr = assetManager.createAsset<GLTFMaterialAsset>(saveInfo, true);
				materialPtr->markDirty();

				auto assignGLTFTexture = [&](GLTFTextureInfo& info, auto& b)
				{
					if (b.index >= 0 && b.index < model.images.size())
					{
						info.image = importedTextures.at(b.index);
						info.textureCoord = b.texCoord;
					}
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

				if(material.alphaMode == "OPAQUE") { materialPtr->alphaMode = EAlphaMode::Opaque; }
				else if (material.alphaMode == "MASK") { materialPtr->alphaMode = EAlphaMode::Mask; }
				else { materialPtr->alphaMode = EAlphaMode::Blend; check(material.alphaMode == "BLEND"); }

				materialPtr->alphaCoutoff = material.alphaCutoff;
				materialPtr->bDoubleSided = material.doubleSided;

				assignGLTFTexture(materialPtr->normalTexture, material.normalTexture);
				materialPtr->normalTextureScale = (float)material.normalTexture.scale;

				assignGLTFTexture(materialPtr->occlusionTexture, material.occlusionTexture);
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
			int32 defaultScene = model.defaultScene > -1 ? model.defaultScene : 0;
			const auto& scene = model.scenes[defaultScene];

			std::unordered_map<int, std::vector<uint32>> meshToPrimMeshes;

			std::set<uint32> usedMeshes;
			for (auto nodeIdx : scene.nodes)
			{
				findUsedMeshes(model, usedMeshes, nodeIdx);
			}

			uint32 nbIndex = 0;
			uint32 primCnt = 0;
			for (const auto& meshId : usedMeshes)
			{
				const auto& mesh = model.meshes[meshId];
				std::vector<uint32> vprim;
				for (const auto& primitive : mesh.primitives)
				{
					if (primitive.mode != 4) // Triangle
					{
						ensureMsgf(false, "Current only support triangle primitive import, but we found non triangle mode.");
						continue; // TODO: Support other type of primitive.
					}

					const auto& posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
					if (primitive.indices > -1)
					{ // Index fetch mode.
						const auto& indexAccessor = model.accessors[primitive.indices];
						nbIndex += static_cast<uint32>(indexAccessor.count);
					}
					else
					{ // just position mode.
						nbIndex += static_cast<uint32>(posAccessor.count);
					}

					vprim.emplace_back(primCnt++);
				}

				// mesh-id = { prim0, prim1, ... }
				meshToPrimMeshes[meshId] = std::move(vprim); 
			}

			gltfBin.primitiveData.indices.reserve(nbIndex);
			
			std::unordered_map<std::string, GLTFPrimitiveMesh> cachePrimMesh;

			auto processMesh = [&](const tinygltf::Model& model, const tinygltf::Primitive& mesh, const std::string& name)
			{
				// Only triangles are supported
				// 0:point, 1:lines, 2:line_loop, 3:line_strip, 4:triangles, 5:triangle_strip, 6:triangle_fan
				if (mesh.mode != 4)
				{
					return;
				}

				GLTFPrimitiveMesh primitiveMesh;
				
				primitiveMesh.name = name;
				if(mesh.material > -1) { primitiveMesh.material = importedMaterials[mesh.material]; }

				// Get vertex offset.
				primitiveMesh.vertexOffset        = gltfBin.primitiveData.positions.size();
				primitiveMesh.colors0Offset       = gltfBin.primitiveData.colors0.size();
				primitiveMesh.textureCoord1Offset = gltfBin.primitiveData.texcoords1.size();

				check(primitiveMesh.vertexOffset == gltfBin.primitiveData.normals.size());
				check(primitiveMesh.vertexOffset == gltfBin.primitiveData.smoothNormals.size());
				check(primitiveMesh.vertexOffset == gltfBin.primitiveData.texcoords0.size());
				check(primitiveMesh.vertexOffset == gltfBin.primitiveData.tangents.size());

				// 
				primitiveMesh.firstIndex = gltfBin.primitiveData.indices.size();

				if (mesh.indices > -1)
				{
					const tinygltf::Accessor& indexAccessor = model.accessors[mesh.indices];
					const tinygltf::BufferView& bufferView = model.bufferViews[indexAccessor.bufferView];
					primitiveMesh.indexCount = static_cast<uint32>(indexAccessor.count);

					auto insertIndices = [&]<typename T>()
					{
						const auto* buf = reinterpret_cast<const T*>(&model.buffers[bufferView.buffer].data[indexAccessor.byteOffset + bufferView.byteOffset]);
						for (auto index = 0; index < indexAccessor.count; index++)
						{
							gltfBin.primitiveData.indices.push_back(buf[index]);
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
					// Primitive without indices, creating them
					const auto& accessor = model.accessors[mesh.attributes.find("POSITION")->second];
					for (auto i = 0; i < accessor.count; i++)
					{
						gltfBin.primitiveData.indices.push_back(i);
					}
						
					primitiveMesh.indexCount = static_cast<uint32>(accessor.count);
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

						const auto& cacheMesh = it->second;
						primitiveMesh.vertexCount = cacheMesh.vertexCount;
						primitiveMesh.vertexOffset = cacheMesh.vertexOffset;
						primitiveMesh.colors0Offset = cacheMesh.colors0Offset;
						primitiveMesh.textureCoord1Offset = cacheMesh.textureCoord1Offset;
					}
				}

				if (!bPrimitiveCache)
				{
					// Position.
					{
						if (!mesh.attributes.contains("POSITION"))
						{
							LOG_ERROR("GLTF file is unvalid: a primitive without POSITION attribute.");
						}

						const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("POSITION")->second];
						const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

						const float* posBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						math::vec3 positionMin = math::vec3( std::numeric_limits<float>::max());
						math::vec3 positionMax = math::vec3(-std::numeric_limits<float>::max());

						// Summary of position, use double to keep precision.
						math::dvec3 positionSum = math::dvec3(0.0);
						for (auto index = 0; index < accessor.count; index++)
						{
							gltfBin.primitiveData.positions.push_back({ posBuffer[0], posBuffer[1], posBuffer[2] });
							posBuffer += 3;

							positionMin = math::min(positionMin, gltfBin.primitiveData.positions.back());
							positionMax = math::max(positionMax, gltfBin.primitiveData.positions.back());

							positionSum += gltfBin.primitiveData.positions.back();
						}

						primitiveMesh.posMax = positionMax;
						primitiveMesh.posMin = positionMin;

						// Position average.
						primitiveMesh.posAverage = positionSum / double(accessor.count);
						
						// Vertex count.
						primitiveMesh.vertexCount = accessor.count;
					}

					// Normal.
					{
						if (mesh.attributes.contains("NORMAL"))
						{
							const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("NORMAL")->second];
							const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

							const float* normalBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
							for (auto index = 0; index < accessor.count; index++)
							{
								gltfBin.primitiveData.normals.push_back({ normalBuffer[0], normalBuffer[1], normalBuffer[2] });
								normalBuffer += 3;
							}
						}
						else
						{
							LOG_TRACE("No normal found in mesh '{}', generating...", primitiveMesh.name);

							std::vector<math::vec3> newNormals(primitiveMesh.vertexCount);
							for (auto i = 0; i < primitiveMesh.indexCount; i += 3)
							{
								uint32 ind0 = gltfBin.primitiveData.indices[primitiveMesh.firstIndex + i + 0];
								uint32 ind1 = gltfBin.primitiveData.indices[primitiveMesh.firstIndex + i + 1];
								uint32 ind2 = gltfBin.primitiveData.indices[primitiveMesh.firstIndex + i + 2];

								const auto& pos0 = gltfBin.primitiveData.positions[ind0 + primitiveMesh.vertexOffset];
								const auto& pos1 = gltfBin.primitiveData.positions[ind1 + primitiveMesh.vertexOffset];
								const auto& pos2 = gltfBin.primitiveData.positions[ind2 + primitiveMesh.vertexOffset];

								const auto v1 = math::normalize(pos1 - pos0);
								const auto v2 = math::normalize(pos2 - pos0);
								const auto n  = math::normalize(glm::cross(v1, v2));

								newNormals[ind0] = n;
								newNormals[ind1] = n;
								newNormals[ind2] = n;
							}
							gltfBin.primitiveData.normals.insert(gltfBin.primitiveData.normals.end(), newNormals.begin(), newNormals.end());
						}
					}

					// Smooth normals.
					{
						std::vector<math::vec3> newSmoothNormals(primitiveMesh.vertexCount);
						for (auto i = 0; i < primitiveMesh.indexCount; i += 3)
						{
							uint32 ind0 = gltfBin.primitiveData.indices[primitiveMesh.firstIndex + i + 0];
							uint32 ind1 = gltfBin.primitiveData.indices[primitiveMesh.firstIndex + i + 1];
							uint32 ind2 = gltfBin.primitiveData.indices[primitiveMesh.firstIndex + i + 2];

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

					// Texture Coordinate 0.
					{
						if (mesh.attributes.contains("TEXCOORD_0"))
						{
							const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("TEXCOORD_0")->second];
							const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

							const float* textureCoord0Buffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
							for (auto index = 0; index < accessor.count; index++)
							{
								gltfBin.primitiveData.texcoords0.push_back({ textureCoord0Buffer[0], textureCoord0Buffer[1] });
								textureCoord0Buffer += 2;
							}
						}
						else
						{
							LOG_TRACE("No texture coord #0 found in mesh '{}', generating...", primitiveMesh.name);
							checkEntry(); // todo; 
						}
					}

					// Tangent.
					{
						if (mesh.attributes.contains("TANGENT"))
						{
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
							LOG_TRACE("No tangent found in mesh '{}', generating...", primitiveMesh.name);
							checkEntry(); // todo; 
						}
					}

					// Texture Coordinate 1.
					if (mesh.attributes.contains("TEXCOORD_1"))
					{
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

				gltfBin.primMeshes.emplace_back(primitiveMesh);
			};
			// Convert all mesh/primitives+ to a single primitive per mesh
			for (const auto& meshId : usedMeshes)
			{
				const auto& mesh = model.meshes[meshId];
				for (const auto& primitive : mesh.primitives)
				{
					processMesh(model, primitive, mesh.name);
				}
			}

		}

		saveAsset(gltfBin, ECompressionMode::Lz4, gltfPtr->getBinPath(), false);

		return gltfPtr->save();
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


}