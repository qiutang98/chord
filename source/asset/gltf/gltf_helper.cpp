#include <asset/gltf/gltf_helper.h>
#include <asset/gltf/gltf.h>
#include <asset/asset_common.h>

#include <asset/asset.h>
#include <graphics/helper.h>
#include <graphics/uploader.h>
#include <application/application.h>
#include <asset/compute_tangent.h>

#include <ui/ui_helper.h>
#include <project.h>

#include <asset/serialize.h>

#include <renderer/gpu_scene.h>
#include <utils/thread.h>
#include <shader/gltf.h>
#include <utils/cityhash.h>

#include <asset/nanite_builder.h>
#include <asset/gltf/gltf_material.h>

namespace chord
{
	static void uiDrawImportConfig(GLTFAssetImportConfigRef config)
	{
		ImGui::Checkbox("##SmoothNormal", &config->bGenerateSmoothNormal); ImGui::SameLine(); ImGui::Text("Generate Smooth Normal");
		ImGui::Checkbox("##Fuse", &config->bFuse); ImGui::SameLine(); ImGui::Text("Fuse Close Vertices");

		if (config->bFuse)
		{
			ImGui::Checkbox("##FuseIgnoreVertexNormal", &config->bFuseIgnoreNormal); ImGui::SameLine(); ImGui::Text("Fuse Without Normal Consider");
		}

		ImGui::Separator();
		ImGui::DragFloat("Meshlet ConeWeight", &config->meshletConeWeight, 0.1f, 0.0f, 1.0f);
	}

	struct LoadMeshOptionalAttribute
	{
		bool bSmoothNormal = false;
		bool bUv1 = false;
		bool bColor0 = false;
	};

	static bool loadMesh(
		const tinygltf::Model& model,
		const tinygltf::Primitive& mesh, 
		const std::string& meshName,
		LoadMeshOptionalAttribute& optional,
		bool bGenerateSmoothNormal,
		std::vector<nanite::Vertex>& outputVertices, 
		std::vector<uint32>& outputIndices,
		math::vec3& meshPosMin,
		math::vec3& meshPosMax,
		math::vec3& meshPosAvg)
	{
		if (!mesh.attributes.contains("POSITION"))
		{
			LOG_ERROR("GLTF file is unvalid: a primitive without POSITION attribute, skip...");
			return false;
		}

		// Prepare indices.
		if (mesh.indices > -1)
		{
			const tinygltf::Accessor& indexAccessor = model.accessors[mesh.indices];
			const tinygltf::BufferView& bufferView  = model.bufferViews[indexAccessor.bufferView];

			outputIndices.resize(indexAccessor.count);
			auto insertIndices = [&]<typename T>()
			{
				const auto* buf = reinterpret_cast<const T*>(&model.buffers[bufferView.buffer].data[indexAccessor.byteOffset + bufferView.byteOffset]);
				for (auto index = 0; index < indexAccessor.count; index++)
				{
					outputIndices[index] = buf[index];
				}
			};
			switch (indexAccessor.componentType)
			{
			case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: { insertIndices.operator()<uint32>(); break; }
			case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: { insertIndices.operator()<uint16>(); break; }
			case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: { insertIndices.operator()<uint8>(); break; }
			default: LOG_ERROR("Index component type %i not supported!\n", indexAccessor.componentType); return false;
			}
		}
		else
		{
			LOG_TRACE("No INDICES found in mesh '{}', use triangle order indexing...", meshName);

			const auto& accessor = model.accessors[mesh.attributes.find("POSITION")->second];
			outputIndices.resize(accessor.count);
			for (auto i = 0; i < accessor.count; i++)
			{
				outputIndices[i] = i;
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
			outputVertices.resize(accessor.count);

			for (auto index = 0; index < accessor.count; index++)
			{
				outputVertices[index].position = { posBuffer[0], posBuffer[1], posBuffer[2] };
				posBuffer += 3;

				positionMin = math::min(positionMin, outputVertices[index].position);
				positionMax = math::max(positionMax, outputVertices[index].position);

				positionSum += outputVertices[index].position;
			}

			meshPosMax = positionMax;
			meshPosMin = positionMin;

			// Position average.
			meshPosAvg = positionSum / double(accessor.count);
		}

		// Normal.
		if (mesh.attributes.contains("NORMAL"))
		{
			const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("NORMAL")->second];
			const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

			const float* normalBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
			check(accessor.count == outputVertices.size());

			for (auto index = 0; index < accessor.count; index++)
			{
				outputVertices[index].normal = { normalBuffer[0], normalBuffer[1], normalBuffer[2] };
				normalBuffer += 3;
			}
		}
		else
		{
			LOG_TRACE("No NORMAL found in mesh '{}', generating...", meshName);

			for (auto i = 0; i < outputIndices.size(); i += 3)
			{
				uint32 ind0 = outputIndices[i + 0];
				uint32 ind1 = outputIndices[i + 1];
				uint32 ind2 = outputIndices[i + 2];

				const auto& pos0 = outputVertices[ind0].position;
				const auto& pos1 = outputVertices[ind1].position;
				const auto& pos2 = outputVertices[ind2].position;

				const auto v1 = math::normalize(pos1 - pos0);
				const auto v2 = math::normalize(pos2 - pos0);
				const auto n  = math::normalize(glm::cross(v1, v2));

				outputVertices[ind0].normal = n;
				outputVertices[ind1].normal = n;
				outputVertices[ind2].normal = n;
			}
		}

		// Texture Coordinate 0.
		const bool bExistTextureCoord0 = mesh.attributes.contains("TEXCOORD_0");
		if (bExistTextureCoord0)
		{
			const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("TEXCOORD_0")->second];
			const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

			const float* textureCoord0Buffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
			check(accessor.count == outputVertices.size());
			for (auto index = 0; index < accessor.count; index++)
			{
				outputVertices[index].uv0 = { textureCoord0Buffer[0], textureCoord0Buffer[1] };
				textureCoord0Buffer += 2;
			}
		}
		else
		{
			LOG_TRACE("No TEXCOORD_0 found in mesh '{}', fill default...", meshName);
			for (auto index = 0; index < outputVertices.size(); index++)
			{
				outputVertices[index].uv0 = { 0.0f, 0.0f };
			}
		}
					
		// Tangent.
		if (bExistTextureCoord0)
		{
			if (mesh.attributes.contains("TANGENT"))
			{
				// Tangent already exist so just import.
				const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("TANGENT")->second];
				const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

				const float* tangentBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				check(accessor.count == outputVertices.size());
				for (auto index = 0; index < accessor.count; index++)
				{
					outputVertices[index].tangent = { tangentBuffer[0], tangentBuffer[1], tangentBuffer[2], tangentBuffer[3] };
					tangentBuffer += 4;
				}
			}
			else
			{
				LOG_TRACE("No tangent found in mesh '{}', generating mikktspace...", meshName);
				if (!computeTangent(outputVertices, outputIndices))
				{
					LOG_ERROR("Mesh '{}' mikktspace generate error, skip...", meshName);
					return false;
				}
			}
		}
		else
		{
			LOG_TRACE("Mesh uv0 use default, src tangent is unvalid and fill with default...", meshName);
			for (auto index = 0; index < outputVertices.size(); index++)
			{
				outputVertices[index].tangent = { 0.0f, 0.0f, 0.0f, 0.0f };
			}
		}

		// Smooth normals.
		optional.bSmoothNormal = bGenerateSmoothNormal;
		if (optional.bSmoothNormal)
		{
			std::vector<math::vec3> newSmoothNormals(outputVertices.size());
			for (auto i = 0; i < outputIndices.size(); i += 3)
			{
				uint32 ind0 = outputIndices[i + 0];
				uint32 ind1 = outputIndices[i + 1];
				uint32 ind2 = outputIndices[i + 2];

				const auto& n0 = outputVertices[ind0].normal;
				const auto& n1 = outputVertices[ind1].normal;
				const auto& n2 = outputVertices[ind2].normal;

				newSmoothNormals[ind0] += n0;
				newSmoothNormals[ind1] += n1;
				newSmoothNormals[ind2] += n2;
			}

			for (auto& n : newSmoothNormals)
			{
				n = math::normalize(n);
			}

			for (uint i = 0; i < outputVertices.size(); i++)
			{
				outputVertices[i].smoothNormal = newSmoothNormals[i];
			}
		}

		/////////////////////////////
		// Optional attributes import.
		// 
					
		// Texture Coordinate 1.
		optional.bUv1 = mesh.attributes.contains("TEXCOORD_1");
		if (optional.bUv1)
		{
			const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("TEXCOORD_1")->second];
			const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

			const float* textureCoord1Buffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
			check(accessor.count == outputVertices.size());
			for (auto index = 0; index < accessor.count; index++)
			{
				outputVertices[index].uv1 = { textureCoord1Buffer[0], textureCoord1Buffer[1] };
				textureCoord1Buffer += 2;
			}
		}

		// Color 0.
		optional.bColor0 = mesh.attributes.contains("COLOR_0");
		if (optional.bColor0)
		{
			const tinygltf::Accessor& accessor = model.accessors[mesh.attributes.find("COLOR_0")->second];
			const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];

			const float* colorBuffer = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
			check(accessor.count == outputVertices.size());

			for (auto index = 0; index < accessor.count; index++)
			{
				outputVertices[index].color0 = { colorBuffer[0], colorBuffer[1], colorBuffer[2], colorBuffer[3]};
				colorBuffer += 4;
			}
		}

		return true;
	}

	bool importFromConfig(GLTFAssetImportConfigRef config)
	{
		const std::filesystem::path& srcPath = config->importFilePath;
		const std::filesystem::path& savePath = config->storeFilePath;

		const auto& projectPaths = Project::get().getPath();
		      auto& assetManager = Application::get().getAssetManager();
		const auto& meta         = GLTFAsset::kAssetTypeMeta;
		const auto srcBaseDir    = srcPath.parent_path();

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
			if (!gltf::isGLTFExtensionSupported(ext))
			{
				LOG_ERROR("No supported ext '{0}' used in gltf model '{1}'.", ext, utf8::utf16to8(srcPath.u16string()));
			}
			else
			{
				LOG_TRACE("GLTF model '{1}' using extension '{0}'...", ext, utf8::utf16to8(srcPath.u16string()));
			}
		}

		// Import all images in gltf.
		const auto importedImages = gltf::importMaterialUsedImages(srcPath, savePath, model);

		// Import all materials.
		const auto importedMaterials = gltf::importMaterials(srcPath, savePath, importedImages, model);

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
					primitiveMesh.material = importedMaterials.at(mesh.material);
				}

				if (!bPrimitiveCache)
				{
					LoadMeshOptionalAttribute optionalAttri { };
					std::vector<nanite::Vertex> rawVertices;
					std::vector<uint32> rawIndices;
					math::vec3 meshPosMin;
					math::vec3 meshPosMax;
					math::vec3 meshPosAvg;

					bool bLoadResult = loadMesh(model, mesh, primitiveMesh.name, optionalAttri, bGenerateSmoothNormal, rawVertices, rawIndices, meshPosMin, meshPosMax, meshPosAvg);
					if (!bLoadResult) { return; }

					// Get vertex offset.
					primitiveMesh.vertexOffset = gltfBin.primitiveData.positions.size();
					primitiveMesh.lod0IndicesOffset = gltfBin.primitiveData.lod0Indices.size();

					check(primitiveMesh.vertexOffset == gltfBin.primitiveData.normals.size());
					check(primitiveMesh.vertexOffset == gltfBin.primitiveData.texcoords0.size());
					check(primitiveMesh.vertexOffset == gltfBin.primitiveData.tangents.size());

					// Optional attribute offset.
					primitiveMesh.colors0Offset       = gltfBin.primitiveData.colors0.size();
					primitiveMesh.textureCoord1Offset = gltfBin.primitiveData.texcoords1.size();
					primitiveMesh.smoothNormalOffset  = gltfBin.primitiveData.smoothNormals.size();

					// Meshlet offset.
					primitiveMesh.meshletOffset = gltfBin.primitiveData.meshlets.size();
					primitiveMesh.meshletGroupOffset = gltfBin.primitiveData.meshletGroups.size();
					primitiveMesh.meshletGroupIndicesOffset = gltfBin.primitiveData.meshletGroupIndices.size();
					primitiveMesh.bvhNodeOffset = gltfBin.primitiveData.bvhNodes.size();
					primitiveMesh.meshletGroupCount = gltfBin.primitiveData.meshletGroups.size();

					// Position min, max and average.
					primitiveMesh.posMin = meshPosMin;
					primitiveMesh.posMax = meshPosMax;
					primitiveMesh.posAverage = meshPosAvg;



					nanite::NaniteBuilder builder(
						std::move(rawIndices), 
						std::move(rawVertices), 
						config->bFuse, 
						config->bFuseIgnoreNormal, 
						config->meshletConeWeight);
					{
						auto meshletCtx = builder.build();

						primitiveMesh.bvhNodeCount = meshletCtx.bvhNodes[0].bvhNodeCount;
						primitiveMesh.meshletGroupCount = meshletCtx.meshletGroups.size();

						gltfBin.primitiveData.meshletGroups.insert(gltfBin.primitiveData.meshletGroups.end(), meshletCtx.meshletGroups.begin(), meshletCtx.meshletGroups.end());
						gltfBin.primitiveData.meshletGroupIndices.insert(gltfBin.primitiveData.meshletGroupIndices.end(), meshletCtx.meshletGroupIndices.begin(), meshletCtx.meshletGroupIndices.end());
						gltfBin.primitiveData.bvhNodes.insert(gltfBin.primitiveData.bvhNodes.end(), meshletCtx.bvhNodes.begin(), meshletCtx.bvhNodes.end());

						for (const auto& meshlet : meshletCtx.meshlets)
						{
							const auto dataOffset = gltfBin.primitiveData.meshletDatas.size();

							// Fill meshlet indices data.
							{
								for (auto i = 0; i < meshlet.info.vertex_count; ++i) 
								{ 
									gltfBin.primitiveData.meshletDatas.push_back(meshletCtx.vertices[meshlet.info.vertex_offset + i]);
								}

								for (auto i = 0; i < meshlet.info.triangle_count; ++i)
								{ 
									uint8 id0 = meshletCtx.triangles[meshlet.info.triangle_offset + i * 3 + 0];
									uint8 id1 = meshletCtx.triangles[meshlet.info.triangle_offset + i * 3 + 1];
									uint8 id2 = meshletCtx.triangles[meshlet.info.triangle_offset + i * 3 + 2];

									uint32 idx = id0;
									idx |= (uint32(id1) << 8);
									idx |= (uint32(id2) << 16);

									gltfBin.primitiveData.meshletDatas.push_back(idx);
								}
							}

							gltfBin.primitiveData.meshlets.push_back(meshlet.getGLTFMeshlet(dataOffset));
						}

						primitiveMesh.lod0meshletCount = 0;
						for (const auto& meshlet : meshletCtx.meshlets)
						{
							if (meshlet.lod == 0) { primitiveMesh.lod0meshletCount ++; }
						}
					}

					const std::vector<nanite::Vertex>& builderVertices = builder.getVertices();
					const std::vector<uint32> lod0Indices = builder.getIndices();

					// 
					primitiveMesh.vertexCount = builderVertices.size();
					primitiveMesh.lod0IndicesCount = lod0Indices.size();

					// Fill exist state.
					primitiveMesh.bColor0Exist = optionalAttri.bColor0;
					primitiveMesh.bSmoothNormalExist = optionalAttri.bSmoothNormal;
					primitiveMesh.bTextureCoord1Exist = optionalAttri.bUv1;

					// Fill lod0 indices. (Used for voxelize, ray tracing or sdf generation, etc.)
					gltfBin.primitiveData.lod0Indices.insert(gltfBin.primitiveData.lod0Indices.end(), lod0Indices.begin(), lod0Indices.end());

					// Insert back to binary data.
					for (auto& vertex : builderVertices)
					{
						gltfBin.primitiveData.texcoords0.push_back(vertex.uv0);
						gltfBin.primitiveData.positions.push_back(vertex.position);
						gltfBin.primitiveData.normals.push_back(vertex.normal);
						gltfBin.primitiveData.tangents.push_back(vertex.tangent);

						if (primitiveMesh.bColor0Exist) { gltfBin.primitiveData.colors0.push_back(vertex.color0); }
						if (primitiveMesh.bTextureCoord1Exist) { gltfBin.primitiveData.texcoords1.push_back(vertex.uv1); }
						if (primitiveMesh.bSmoothNormalExist) { gltfBin.primitiveData.smoothNormals.push_back(vertex.smoothNormal); }
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

	graphics::BuiltinMeshRef loadBuiltinMeshFromPath(const std::string& loadPath)
	{
		graphics::BuiltinMeshRef result = std::make_shared<graphics::BuiltinMesh>();
		result->meshTypeUniqueId = crc::crc32(loadPath.c_str(), loadPath.size(), 0);

		tinygltf::Model model;
		{
			tinygltf::TinyGLTF tcontext;
			std::string warning;
			std::string error;

			std::filesystem::path srcPath = loadPath;

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

			// Must success for builtin mesh loading.
			check(bSuccess);
		}

		// Builtin mesh only support one mesh.
		check(model.meshes.size() == 1); 
		// Only support one primitive.
		check(model.meshes[0].primitives.size() == 1);


		std::vector<nanite::Vertex> rawVertices;
		std::vector<uint32> rawIndices;
		{
			LoadMeshOptionalAttribute optionalAttri{ };
			math::vec3 meshPosMin;
			math::vec3 meshPosMax;
			math::vec3 meshPosAvg;
			check(loadMesh(model, model.meshes[0].primitives[0], loadPath, optionalAttri, false, rawVertices, rawIndices, meshPosMin, meshPosMax, meshPosAvg));
		}

		using namespace graphics;

		result->indicesCount = rawIndices.size();
		std::vector<BuiltinMesh::BuiltinVertex> vertices(rawVertices.size());

		for(uint32 i = 0; i < rawVertices.size(); i ++)
		{
			vertices[i].position = rawVertices[i].position;
			vertices[i].uv       = rawVertices[i].uv0;
			vertices[i].normal   = rawVertices[i].normal;
		}

		result->indices = getContext().getBufferPool().createHostVisibleCopyUpload(
			loadPath + "_indices",
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			SizedBuffer(sizeof(rawIndices[0]) * rawIndices.size(), (void*)rawIndices.data()));

		result->vertices = getContext().getBufferPool().createHostVisibleCopyUpload(
			loadPath + "_vertices",
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			SizedBuffer(sizeof(vertices[0]) * vertices.size(), (void*)vertices.data()));

		return result;
	}
}