#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/optional.h>
#include <graphics/common.h>
#include <utils/job_system.h>


namespace chord::graphics
{
	// Batch multi tasks with same shader file data.
	struct BatchShaderCompileTasks
	{
		std::string fileName;

		struct Batch
		{
			std::string name;
			std::filesystem::path tempStorePath;
			ShaderCompileArguments arguments;

			// Operating shader module.
			std::shared_ptr<ShaderModule> shaderModule;
		};

		std::vector<Batch> batches;
	};

	// Result of shader compiler.
	struct ShaderCompileResult
	{
		// Compile state result.
		bool bSuccess;
		std::string errorMsg;

		std::vector<uint8> shader;
		std::vector<uint8> pdb;
		std::vector<uint8> reflection;
	};

	class IPlatformShaderCompiler : NonCopyable
	{
	public:
		explicit IPlatformShaderCompiler() = default;
		virtual ~IPlatformShaderCompiler() = default;

		//
		virtual void compileShader(
			SizedBuffer shaderData, 
			const std::vector<std::string>& args,
			ShaderCompileResult& result) const = 0;
	};

	class ShaderCompilerManager : NonCopyable
	{
		using TaskType = std::function<void(IPlatformShaderCompiler&)>;

	public:
		explicit ShaderCompilerManager(uint32 freeCount, uint32 desiredMaxCompileThreadCount);

		const IPlatformShaderCompiler& getPlatformCompiler() const
		{
			return *m_compiler; 
		} 

	protected:
		std::unique_ptr<IPlatformShaderCompiler> m_compiler;
	};
}