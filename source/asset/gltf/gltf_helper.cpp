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

#include <asset/xatlas.h>
#include <asset/mikktspace.h>

#include <asset/meshoptimizer/meshoptimizer.h>

namespace chord
{
	constexpr size_t kMeshletMaxVertices  = 64;
	constexpr size_t kMeshletMaxTriangles = 124;
	constexpr float  kMeshletConeWeight = 0.5f;
	constexpr double kGLTFLODStepReduceFactor = 0.75;
	constexpr float  kGLTFLODTargetError = 1e-2f;

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
		ImGui::Checkbox("Smooth Normal", &config->bGenerateSmoothNormal);
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

				if (material.alphaMode == "OPAQUE") { materialPtr->alphaMode = EAlphaMode::Opaque; }
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
					{
						meshopt_optimizeVertexCache(tempIndices.data(), tempIndices.data(), tempIndices.size(), tempPositions.size());

						auto computeMeshlet = [&](const std::vector<math::vec3>& positions, const std::vector<uint32>& indices)
						{
							std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(indices.size(), kMeshletMaxVertices, kMeshletMaxTriangles));
							std::vector<uint32> meshletVertices(meshlets.size() * kMeshletMaxVertices);
							std::vector<uint8> meshletTriangles(meshlets.size() * kMeshletMaxTriangles * 3);

							meshlets.resize(meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), indices.data(), indices.size(), &positions[0].x, positions.size(), sizeof(positions[0]), kMeshletMaxVertices, kMeshletMaxTriangles, kMeshletConeWeight));

							for (const auto& meshlet : meshlets)
							{
								meshopt_optimizeMeshlet(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, meshlet.vertex_count);

								uint32 dataOffset = gltfBin.primitiveData.meshletData.size();
								for (uint32 i = 0; i < meshlet.vertex_count; i++)
								{
									gltfBin.primitiveData.meshletData.push_back(meshletVertices[meshlet.vertex_offset + i]);
								}

								const uint32* indexGroups = reinterpret_cast<const uint32*>(&meshletTriangles[0] + meshlet.triangle_offset);
								uint32 indexGroupCount = (meshlet.triangle_count * 3 + 3) / 4;

								for (uint32 i = 0; i < indexGroupCount; ++i)
								{
									gltfBin.primitiveData.meshletData.push_back(indexGroups[i]);
								}

								meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshletVertices[meshlet.vertex_offset], &meshletTriangles[meshlet.triangle_offset], meshlet.triangle_count, &positions[0].x, positions.size(), sizeof(positions[0]));
								GLTFMeshlet m = { };

								m.dataOffset    = dataOffset;
								m.triangleCount = meshlet.triangle_count;
								m.vertexCount   = meshlet.vertex_count;
								
								math::vec3 posMin = math::vec3( FLT_MAX);
								math::vec3 posMax = math::vec3(-FLT_MAX);
								math::dvec3 posAvg = math::dvec3(0);
								uint32 indexCount = 0;
								for (uint32 triangleId = 0; triangleId < meshlet.triangle_count; triangleId++)
								{
									uint8 id0 = meshletTriangles[meshlet.triangle_offset + triangleId * 3 + 0];
									uint8 id1 = meshletTriangles[meshlet.triangle_offset + triangleId * 3 + 1];
									uint8 id2 = meshletTriangles[meshlet.triangle_offset + triangleId * 3 + 2];

									uint32 index0 = meshletVertices[meshlet.vertex_offset + id0];
									uint32 index1 = meshletVertices[meshlet.vertex_offset + id1];
									uint32 index2 = meshletVertices[meshlet.vertex_offset + id2];

									posMax = math::max(posMax, positions[index0]);
									posMax = math::max(posMax, positions[index1]);
									posMax = math::max(posMax, positions[index2]);

									posMin = math::min(posMin, positions[index0]);
									posMin = math::min(posMin, positions[index1]);
									posMin = math::min(posMin, positions[index2]);

									indexCount += 3;
									posAvg += positions[index0];
									posAvg += positions[index1];
									posAvg += positions[index2];
								}

								m.posMin = posMin;
								m.posMax = posMax;
								posAvg /= double(indexCount);
								m.posAverage = posAvg;

								gltfBin.primitiveData.meshlets.push_back(m);
							}

							return meshlets.size();
						};

						std::vector<uint32> lodIndices = std::move(tempIndices);
						while (primitiveMesh.lods.size() < kMaxGLTFLodCount)
						{
							primitiveMesh.lods.push_back({});
							GLTFPrimitiveLOD& lod = primitiveMesh.lods.back();

							lod.firstIndex   = gltfBin.primitiveData.indices.size();
							lod.indexCount   = static_cast<uint32>(lodIndices.size());
							lod.firstMeshlet = gltfBin.primitiveData.meshlets.size();
							lod.meshletCount = computeMeshlet(tempPositions, lodIndices);

							// Insert to bin data.
							gltfBin.primitiveData.indices.insert(gltfBin.primitiveData.indices.end(), lodIndices.begin(), lodIndices.end());

							if (primitiveMesh.lods.size() < kMaxGLTFLodCount)
							{
								size_t nextLodIndicesTarget = size_t(double(lodIndices.size()) * kGLTFLODStepReduceFactor);
								size_t nextIndices = meshopt_simplify(lodIndices.data(), lodIndices.data(), lodIndices.size(), &tempPositions[0].x, tempPositions.size(), sizeof(tempPositions[0]), nextLodIndicesTarget, kGLTFLODTargetError);
								
								check(nextIndices <= lodIndices.size());
								if (nextIndices == lodIndices.size())
								{
									// Reach error bound, pre-return.
									break;
								}

								lodIndices.resize(nextIndices);
								meshopt_optimizeVertexCache(lodIndices.data(), lodIndices.data(), lodIndices.size(), tempPositions.size());
							}
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
							return ctx->mesh->lods[0].indexCount / 3;
						};
						ctx.m_pInterface->m_getNumVerticesOfFace = [](const SMikkTSpaceContext* pContext, const int iFace) -> int
						{
							return 3;
						};
						ctx.m_pInterface->m_getPosition = [](const SMikkTSpaceContext* pContext, float fvPosOut[], const int iFace, const int iVert) -> void
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							uint32 id = ctx->data->indices[iFace * 3 + iVert];
							fvPosOut[0] = ctx->data->positions[id].x;
							fvPosOut[1] = ctx->data->positions[id].y;
							fvPosOut[2] = ctx->data->positions[id].z;
						};
						ctx.m_pInterface->m_getNormal = [](const SMikkTSpaceContext* pContext, float fvNormOut[], const int iFace, const int iVert) -> void
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							uint32 id = ctx->data->indices[iFace * 3 + iVert];

							fvNormOut[0] = ctx->data->normals[id].x;
							fvNormOut[1] = ctx->data->normals[id].y;
							fvNormOut[2] = ctx->data->normals[id].z;
						};
						ctx.m_pInterface->m_getTexCoord = [](const SMikkTSpaceContext* pContext, float fvTexcOut[], const int iFace, const int iVert) -> void
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							uint32 id = ctx->data->indices[iFace * 3 + iVert];

							fvTexcOut[0] = ctx->data->texcoords0[id].x;
							fvTexcOut[1] = ctx->data->texcoords0[id].y;
						};
						ctx.m_pInterface->m_setTSpaceBasic = [](const SMikkTSpaceContext* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert)
						{
							auto* ctx = (MikkTSpaceContext*)pContext->m_pUserData;
							uint32 id = ctx->data->indices[iFace * 3 + iVert];

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


	GPUGLTFPrimitiveAsset::GPUGLTFPrimitiveAsset(const std::string& inName)
		: IUploadAsset(nullptr)
		, m_name(inName)
	{

	}

	GPUGLTFPrimitiveAsset::ComponentBuffer::ComponentBuffer(
		const std::string& name, 
		VkBufferUsageFlags flags, 
		VmaAllocationCreateFlags vmaFlags,
		size_t stripe,
		size_t num)
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

	GPUGLTFPrimitiveAsset::ComponentBuffer::~ComponentBuffer()
	{
		if (bindless.isValid())
		{
			const bool bReleasing = (Application::get().getRuntimePeriod() == ERuntimePeriod::Releasing);
			graphics::GPUBufferRef fallback = bReleasing ? nullptr : graphics::getContext().getDummySSBO();

			// Free buffer bindless index.
			graphics::getContext().getBindlessManger().freeStorageBuffer(bindless, fallback);
		}
	}

	size_t GPUGLTFPrimitiveAsset::getSize() const
	{
		auto getValidSize = [](const std::unique_ptr<ComponentBuffer>& buffer) -> size_t 
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
			+ getValidSize(meshlet)
			+ getValidSize(meshletData);
	}

}