#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/optional.h>
#include <graphics/common.h>
#include <utils/threadpool.h>


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
		explicit IPlatformShaderCompiler();
		virtual ~IPlatformShaderCompiler();

		//
		virtual void compileShader(
			SizedBuffer shaderData, 
			const std::vector<std::string>& args,
			ShaderCompileResult& result) = 0;
	};

	class ShaderCompilerManager : public LambdaThreadPool<IPlatformShaderCompiler&>
	{
		using Super = LambdaThreadPool<IPlatformShaderCompiler&>;
		using TaskType = std::function<void(IPlatformShaderCompiler&)>;

	public:
		explicit ShaderCompilerManager(uint32 freeCount, uint32 desiredMaxCompileThreadCount);
		virtual ~ShaderCompilerManager();

		void pushTask(const TaskType&& task)
		{
			{
				const std::lock_guard tasksLock(m_taskQueueMutex);
				m_tasksQueue.push(TaskType(task));
			}

			++m_tasksQueueTotalNum;
			m_cvTaskAvailable.notify_one();
		}

		CHORD_NODISCARD std::future<void> submit(const TaskType&& task)
		{
			auto taskPromise = std::make_shared<std::promise<void>>();
			pushTask([task, taskPromise](IPlatformShaderCompiler& compiler)
			{
				task(compiler);
				taskPromise->set_value();
			});
			return taskPromise->get_future();
		}

	protected:
		virtual void worker() override;
	};
}