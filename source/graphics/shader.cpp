#include <graphics/shader.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/shadercompiler.h>

namespace chord::graphics
{
	static std::string gShaderCompilerUUID = "2024-03-23 20:15";
	static AutoCVarRef<std::string> cVarShaderCompilerUUID(
		"r.graphics.shadercompiler.uuid",
		gShaderCompilerUUID,
		"UUID of temp shader file.",
		EConsoleVarFlags::ReadOnly);

	static std::string gShaderCompileOutputFolder = "save/shader";
	static AutoCVarRef<std::string> cVarShaderCompileOutputFolder(
		"r.graphics.shadercompiler.tempfolder",
		gShaderCompileOutputFolder,
		"Save folder path of temp shader file.",
		EConsoleVarFlags::ReadOnly);

	uint64 graphics::getShaderModuleHash(int32 permutationId, const GlobalShaderRegisteredInfo& info)
	{
		uint64 hash = uint64(int64(std::numeric_limits<int32>::max()) + int64(permutationId) + 1);

		hash = cityhash::cityhash64WithSeeds(info.shaderFilePath.data(), info.shaderFilePath.size(), hash, uint64(info.permutationCount));
		hash = cityhash::cityhash64WithSeeds(info.entry.data(), info.shaderFilePath.size(), hash, uint64(info.stage));

		return hash;
	}

	ShaderModule::ShaderModule(SizedBuffer buffer)
	{
		create(buffer);
	}

	ShaderModule::~ShaderModule()
	{
		clear();
	}

	void ShaderModule::create(SizedBuffer buffer)
	{
		std::lock_guard lock(m_lock);

		// Try clear.
		clear();

		if (!m_shader.isValid())
		{
			VkShaderModuleCreateInfo ci{};
			ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			ci.codeSize = buffer.size;
			ci.pCode = (uint32*)buffer.ptr;

			VkShaderModule shaderModule;
			checkVkResult(vkCreateShaderModule(getDevice(), &ci, getAllocationCallbacks(), &shaderModule));

			// Assign result.
			m_shader = shaderModule;

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
			const auto& env = infos[i].env;
			const auto& hash = infos[i].hash;

			// Get shader temp file store path.
			const auto tempFilePath = std::filesystem::path(gShaderCompileOutputFolder) / std::to_string(hash);

			// Create from shader compiler then store it.
			BatchShaderCompileTasks::Batch compileBatch
			{ 
				.name = env.getName(), 
				.tempStorePath = tempFilePath
			};

			// Generate shader args.
			env.buildArgs(compileBatch.arguments);

			// Create empty shader state.
			compileBatch.shaderModule = std::make_shared<ShaderModule>(SizedBuffer{});
			m_shaderCollection[hash] = compileBatch.shaderModule;
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

	void ShaderLibrary::release()
	{
		m_shaderFiles.clear();
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

	void GlobalShader::modifyCompileEnvironment(ShaderCompileEnvironment& o)
	{
		
	}
}

