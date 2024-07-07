#include <shader/shader.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <shader/compiler.h>
#include <shader/spirv_reflect.h>

namespace chord::graphics
{
	static u16str gShaderCompileOutputFolder = u16str("save/shader");
	static AutoCVarRef<u16str> cVarShaderCompileOutputFolder(
		"r.graphics.shadercompiler.tempfolder",
		gShaderCompileOutputFolder,
		"Save folder path of temp shader file.",
		EConsoleVarFlags::ReadOnly);

	static u16str sRecompileShaderFile = u16str("");
	static AutoCVarRef<u16str> cVarRecompileShaderFile(
		"r.shader.recompile",
		sRecompileShaderFile,
		"Recompile shader file in the fly, if param equal 'all', will recompile all shaders."
		"If you want to recompile single shader file, just fill it path like 'resource/shader/gltf.hlsl'."
	);

	// Generate shader module hash by permutation id and meta info id.
	uint64 graphics::getShaderModuleHash(int32 permutationId, const GlobalShaderRegisteredInfo& info)
	{
		return hashCombine(info.getHash(), permutationId);
	}

	ShaderModule::ShaderModule(SizedBuffer buffer, const GlobalShaderRegisteredInfo& metaInfo)
		: m_metaInfo(metaInfo)
	{
		create(buffer);
	}

	ShaderModule::~ShaderModule()
	{
		clear();
	}

	PipelineShaderStageCreateInfo ShaderModule::getShaderStageCreateInfo() const
	{
		PipelineShaderStageCreateInfo stage { };

		switch (m_metaInfo.stage)
		{
		case EShaderStage::Vertex:  stage.stage = VK_SHADER_STAGE_VERTEX_BIT;   break;
		case EShaderStage::Pixel:   stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT; break;
		case EShaderStage::Compute: stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;  break;
		case EShaderStage::Amplify: stage.stage = VK_SHADER_STAGE_TASK_BIT_EXT; break;
		case EShaderStage::Mesh:    stage.stage = VK_SHADER_STAGE_MESH_BIT_EXT; break;
		default: checkEntry(); break;
		}

		stage.module = m_shader.get();
		stage.pName = m_metaInfo.entry.c_str();

		return stage;
	}

	void ShaderModule::create(SizedBuffer buffer)
	{
		std::lock_guard lock(m_lock);

		// Try clear.
		clear();

		if (!m_shader.isValid() && buffer.isValid())
		{
			VkShaderModuleCreateInfo ci{};
			ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			ci.codeSize = buffer.size;
			ci.pCode = (uint32*)buffer.ptr;

			VkShaderModule shaderModule;
			checkVkResult(vkCreateShaderModule(getDevice(), &ci, getAllocationCallbacks(), &shaderModule));

			// Assign result.
			m_shader = shaderModule;

			// Generate reflection data for a shader
			{
				SpvReflectShaderModule moduleReflect;
				SpvReflectResult result = spvReflectCreateShaderModule(buffer.size, buffer.ptr, &moduleReflect);
				check(result == SPV_REFLECT_RESULT_SUCCESS);

				uint32 count = 0;

				// Push const.
				{
					result = spvReflectEnumeratePushConstantBlocks(&moduleReflect, &count, NULL);
					check(result == SPV_REFLECT_RESULT_SUCCESS);
					if (count > 0)
					{
						// Generally all shader only exist one push const.
						check(count == 1);

						std::vector<SpvReflectBlockVariable*> pushConstant(count);
						result = spvReflectEnumeratePushConstantBlocks(&moduleReflect, &count, pushConstant.data());
						check(result == SPV_REFLECT_RESULT_SUCCESS);

						m_pushConstSize = pushConstant[0]->size;
					}
				}

				// Destroy the reflection data when no longer required.
				spvReflectDestroyShaderModule(&moduleReflect);
			}

			// Update compile state to ready.
			m_compileState = ECompileState::Ready;
		}
	}

	void ShaderModule::clear()
	{
		if (m_shader.isValid())
		{
			helper::destroyShaderModule(m_shader.get());
		}
		m_shader = { };

		// Clean compile state.
		m_compileState = ECompileState::MAX;
	}

	ShaderFile::ShaderFile(const std::string& path)
		: m_filePath(path)
	{

	}

	FutureCollection<void> ShaderFile::prepareBatchCompile(const ShaderPermutationBatchCompile& batch)
	{
		FutureCollection<void> compilerFutures{ };
		BatchShaderCompileTasks tasks { .fileName = m_filePath };

		// Generate per env shader compile task.
		const auto& infos = batch.getBatches();
		for (auto i = 0; i < infos.size(); i ++)
		{
			const auto& env  = infos[i].env;
			const auto& hash = infos[i].hash;

			// Get shader temp file store path.
			const auto tempFilePath = 
				std::filesystem::path(gShaderCompileOutputFolder.u16()) / 
				std::filesystem::path(env.getMetaInfo().shaderFilePath).stem() / 
				std::to_string(hash);

			// Create from shader compiler then store it.
			BatchShaderCompileTasks::Batch compileBatch
			{ 
				.name = env.getMetaInfo().shaderName, 
				.tempStorePath = tempFilePath
			};

			// Generate shader args.
			env.buildArgs(compileBatch.arguments);

			// Create empty shader state.
			compileBatch.shaderModule = std::make_shared<ShaderModule>(SizedBuffer{}, env.getMetaInfo());
			m_shaderCollection[hash] = compileBatch.shaderModule;

			tasks.batches.push_back(std::move(compileBatch));
		}

		compilerFutures.futures.push_back(getContext().getShaderCompiler().submit([tasks](IPlatformShaderCompiler& compiler)
		{
			std::vector<char> shaderSrcFileData {};
			for (auto& batch : tasks.batches)
			{
				// Try load from temp store.
				if (std::filesystem::exists(batch.tempStorePath))
				{
					std::vector<char> blobData;
					if (loadFile(batch.tempStorePath, blobData, "rb"))
					{
						// Success.
						batch.shaderModule->create(blobData);
						continue;
					}
				}

				if (shaderSrcFileData.empty())
				{
					if (!loadFile(tasks.fileName, shaderSrcFileData, "rb"))
					{
						LOG_ERROR("Shader src file {0} load fail, shader {1} compile error.", tasks.fileName, batch.name);
						batch.shaderModule->setCompileState(ShaderModule::ECompileState::Error);
						continue;
					}
				}

				// Still unvalid, recompile.
				ShaderCompileResult compileResult;
				compiler.compileShader(shaderSrcFileData, batch.arguments, compileResult);

				if (compileResult.bSuccess)
				{
					// Create shader temp save folder if no exist.
					const auto saveFolder = batch.tempStorePath.parent_path();
					{
						static std::mutex createFolderMutex;
						std::lock_guard lock(createFolderMutex);
						if (!std::filesystem::exists(saveFolder))
						{
							std::filesystem::create_directories(saveFolder);
						}
					}

					// Try store compile file in temp path.
					storeFile(batch.tempStorePath, compileResult.shader.data(), compileResult.shader.size(), "wb");

					// Success.
					batch.shaderModule->create(compileResult.shader);
				}
				else
				{
					// Failed.
					LOG_GRAPHICS_ERROR("Shader '{0}' in file '{1}' compile error, see the detailed msg and fix it.\n {2}",
						batch.name, tasks.fileName, compileResult.errorMsg);
					batch.shaderModule->setCompileState(ShaderModule::ECompileState::Error);
				}
			}
		}));

		return compilerFutures;
	}

	void ShaderLibrary::tick(const ApplicationTickData& tickData)
	{
		handleRecompile();
	}

	void ShaderLibrary::release()
	{
		m_shaderFiles.clear();
	}

	void ShaderLibrary::handleRecompile()
	{
		if (!sRecompileShaderFile.empty())
		{
			const bool bAllRecompile = (sRecompileShaderFile.u8() == "all");
			std::shared_ptr<ShaderFile> targetShaderFile = nullptr;

			if (bAllRecompile)
			{
				LOG_TRACE("Recompiling all shaders used in the application...");
			}
			else
			{
				targetShaderFile = getShaderFile(sRecompileShaderFile.u8());
				if (!targetShaderFile)
				{
					return;
				}
			}

			getContext().waitDeviceIdle();

			const auto& table = GlobalShaderRegisterTable::get().getTable();
			for (const auto& registerInfo : table)
			{
				const auto& info = registerInfo.second;
				if (bAllRecompile || info->shaderFilePath == sRecompileShaderFile.u8())
				{
					info->updateShaderFileHash();
				}
			}

			auto& batches = GlobalShaderRegisterTable::get().getBatchCompile();
			for (auto& batch : batches)
			{
				if (bAllRecompile || batch.getMetaInfo().shaderFilePath == sRecompileShaderFile.u8())
				{
					batch.updateBatchesHash();
				}
			}

			const auto& batchCompile = GlobalShaderRegisterTable::get().getBatchCompile();
			FutureCollection<void> futures{};

			for (const auto& batch : batchCompile)
			{
				const auto& registerInfo = batch.getRegisteredInfo();
				if (bAllRecompile || targetShaderFile == getShaderFile(registerInfo.shaderFilePath))
				{
					futures.combine(std::move(targetShaderFile->prepareBatchCompile(batch)));
				}
			}
			futures.wait();

			// Reset shader file.
			sRecompileShaderFile = u16str("");
		}
	}

	void ShaderLibrary::init()
	{
		const auto& batchCompile = GlobalShaderRegisterTable::get().getBatchCompile();

		FutureCollection<void> futures{};
		for (const auto& batch : batchCompile)
		{
			const auto& registerInfo = batch.getRegisteredInfo();
			auto shaderFile = getShaderFile(registerInfo.shaderFilePath);
			futures.combine(std::move(shaderFile->prepareBatchCompile(batch)));
		}

		// Wait all builtin task finish.
		futures.wait();
	}

	std::shared_ptr<ShaderFile> ShaderLibrary::getShaderFile(const std::string& path)
	{
		uint64 hashFile = cityhash::cityhash64(path.c_str(), path.size());
		if (m_shaderFiles[hashFile] == nullptr)
		{
			m_shaderFiles[hashFile] = std::make_shared<ShaderFile>(path);
		}
		return m_shaderFiles[hashFile];
	}

	GlobalShaderRegisterTable& GlobalShaderRegisterTable::get()
	{
		static GlobalShaderRegisterTable shaderTable{};
		return shaderTable;
	}
}

