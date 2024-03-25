#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/optional.h>
#include <graphics/graphics.h>
#include <graphics/shaderpermutation.h>
#include <utils/threadpool.h>

namespace chord::graphics
{
	// Shader contain multi permutation.
	class ShaderModule : NonCopyable
	{
	public:
		enum ECompileState
		{
			// Shader ready.
			Ready,

			// Shader compile error.
			Error,

			// Default.
			MAX
		};

		explicit ShaderModule(SizedBuffer buffer);
		virtual ~ShaderModule();

		VkShaderModule get() const
		{
			std::lock_guard lock(m_lock);
			return m_shader.get();
		}

		bool isReady() const
		{
			std::lock_guard lock(m_lock);
			return m_compileState == ECompileState::Ready;
		}

		bool isError() const
		{
			std::lock_guard lock(m_lock);
			return m_compileState == ECompileState::Error;
		}

		ECompileState getCompileState() const
		{
			std::lock_guard lock(m_lock);
			return m_compileState;
		}

		void setCompileState(ECompileState state)
		{
			std::lock_guard lock(m_lock);
			m_compileState = state;
		}

		void create(SizedBuffer buffer);

	protected:
		void clear();

	protected:
		mutable std::mutex m_lock;

		ECompileState m_compileState = ECompileState::MAX;

		// Cache shader modules.
		OptionalVkShaderModule m_shader;
	};
	using ShaderModuleRef = std::shared_ptr<ShaderModule>;

	struct GlobalShaderRegisteredInfo
	{
		std::string shaderFilePath;
		std::string entry;
		EShaderStage stage;
		int32 permutationCount;
	};

	extern uint64 getShaderModuleHash(int32 permutationId, const GlobalShaderRegisteredInfo& info);

	struct ShaderPermutationCompileInfo
	{
		ShaderCompileEnvironment env;
		uint64 hash;
		int32 permutationId;
	};

	class ShaderPermutationBatchCompile
	{
	public:
		explicit ShaderPermutationBatchCompile(const GlobalShaderRegisteredInfo& info)
			: m_registedInfo(info)
		{

		}

		template<typename Permutation>
		auto& add(const Permutation& permutation)
		{
			ShaderPermutationCompileInfo info { };

			info.permutationId = permutation.toId();
			info.hash = getShaderModuleHash(info.permutationId, m_registedInfo);

			// Modify compilation environment.
			permutation.modifyCompileEnvironment(info.env);

			// Add batch.
			m_batches.push_back(std::move(info));
			return *this;
		}

		auto& addDummy()
		{
			ShaderPermutationCompileInfo info{ };

			info.permutationId = 0;
			info.hash = getShaderModuleHash(info.permutationId, m_registedInfo);

			// Add batch.
			m_batches.push_back(std::move(info));
			return *this;
		}

		const auto& getBatches() const
		{
			return m_batches;
		}

	private:
		const GlobalShaderRegisteredInfo& m_registedInfo;
		std::vector<ShaderPermutationCompileInfo> m_batches;
	};

	// Shader file collect all permutation create from it.
	class ShaderFile : NonCopyable
	{
	public:
		explicit ShaderFile(const std::string& path);

		// Get or create shader module from definitions.
		template<typename Permutation>
		const ShaderModuleRef getShaderModule(const GlobalShaderRegisteredInfo& info, Permutation permutation)
		{
			uint64 hash = getShaderModuleHash(permutation.toId(), info);
			return m_shaderCollection.at(hash);
		}

		const ShaderModuleRef getShaderModule(const GlobalShaderRegisteredInfo& info, int32 permutationId = 0)
		{
			uint64 hash = getShaderModuleHash(permutationId, info);
			return m_shaderCollection.at(hash);
		}

		FutureCollection<void> prepareBatchCompile(const ShaderPermutationBatchCompile& batch);

	private:
		// Shader file paths.
		std::string m_filePath;

		// Shader permutation collection.
		std::map<uint64, std::shared_ptr<ShaderModule>> m_shaderCollection;
	};

	class GlobalShader : NonCopyable
	{
	public:
		// 
		static void modifyCompileEnvironment(ShaderCompileEnvironment& o);

	};



	class GlobalShaderRegisterTable : NonCopyable
	{
		template<class ShaderType>
		friend class GlobalShaderRegister;
	public:

		static GlobalShaderRegisterTable& get();
		const auto& getTable() const { return m_registeredShaders; }

	private:
		GlobalShaderRegisterTable() = default;
		
		std::unordered_map<size_t, GlobalShaderRegisteredInfo> m_registeredShaders;
		std::vector<ShaderPermutationBatchCompile> m_batchCompile;
	};

	template<typename T>
	concept has_some_type = requires { typename T::Permutation; };

	template<class ShaderType>
	class GlobalShaderRegister : NonCopyable
	{
	public:
		GlobalShaderRegister(const char* file, const char* entry, EShaderStage stage)
		{
			auto& table   = GlobalShaderRegisterTable::get().m_registeredShaders;
			auto& batches = GlobalShaderRegisterTable::get().m_batchCompile;

			const auto& typeHash = getTypeHash<ShaderType>();
			const auto  registerInfo = GlobalShaderRegisteredInfo{ .entry = entry, .shaderFilePath = file, .stage = stage };

			// Register global shader meta info.
			table[typeHash] = registerInfo;

			// Fetch all shader permutations.
			if constexpr (requires { typename ShaderType::Permutation; })
			{
				using Permutation = ShaderType::Permutation;
				constexpr uint32 permutationCount = Permutation::kCount;

				ShaderPermutationBatchCompile batch(registerInfo);
				for (auto i = 0; i < permutationCount; i++)
				{
					batch.add(Permutation::fromId(i));
				}
				batches.push_back(std::move(batch));
			}
			else
			{
				ShaderPermutationBatchCompile batch(registerInfo);
				batch.addDummy();

				batches.push_back(std::move(batch));
			}
		}
	};
	#define IMPLEMENT_GLOBAL_SHADER(type, file, entry, stage) namespace { const static GlobalShaderRegister<type> o(file, entry, stage); }
		

	// All shader cache library.
	class ShaderLibrary : NonCopyable
	{
	public:
		// Get shader file.
		std::shared_ptr<ShaderFile> getShaderFile(const std::string& path);

		// Get shader with permutation.
		template<typename ShaderType, typename PermutationType = int32>
		std::shared_ptr<ShaderModule> getShader(PermutationType permutation = 0) const
		{
			const auto& globalShaderTable = GlobalShaderRegisterTable::get().getTable();
			const auto& typeHash = getTypeHash<ShaderType>();

			// Find global shader info.
			const GlobalShaderRegisteredInfo& info = globalShaderTable.at(typeHash);

			// Now get shader file.
			auto shaderFile = getShaderFile(info.shaderFilePath);

			// Now get permutation.
			return shaderFile->getShaderModule(info, permutation);
		}

	private:
		void release();

	private:
		std::map<uint64, std::shared_ptr<ShaderFile>> m_shaderFiles;
	};
}