#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/optional.h>
#include <graphics/graphics.h>
#include <shader/permutation.h>
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

		explicit ShaderModule(SizedBuffer buffer, const GlobalShaderRegisteredInfo& metaInfo);
		virtual ~ShaderModule();

		const GlobalShaderRegisteredInfo& getMetaInfo() const { return m_metaInfo; }

		PipelineShaderStageCreateInfo getShaderStageCreateInfo() const;

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

		uint32 getPushConstSize() const { return m_pushConstSize; }

	protected:
		void clear();


	protected:
		const GlobalShaderRegisteredInfo& m_metaInfo;

		mutable std::mutex m_lock;
		ECompileState m_compileState = ECompileState::MAX;

		// Cache shader modules.
		OptionalVkShaderModule m_shader;

		// Shader module push const size.
		uint32 m_pushConstSize = 0;
	};
	using ShaderModuleRef = std::shared_ptr<ShaderModule>;

	extern uint64 getShaderModuleHash(int32 permutationId, const GlobalShaderRegisteredInfo& info);

	struct ShaderPermutationCompileInfo
	{
		explicit ShaderPermutationCompileInfo(const GlobalShaderRegisteredInfo& info, int32 inPermutationId)
			: env(info)
			, permutationId(inPermutationId)
		{
			updateHash();
		}

		void updateHash()
		{
			hash = getShaderModuleHash(permutationId, env.getMetaInfo());
		}

		ShaderCompileEnvironment env;
		uint64 hash;
		int32 permutationId;
	};

	class ShaderPermutationBatchCompile
	{
	private:
		template<typename ShaderType>
		void modifyCompileEnvironment(ShaderCompileEnvironment& o, int32 PermutationId)
		{
			if constexpr (std::is_invocable_v<decltype(&ShaderType::modifyCompileEnvironment), ShaderCompileEnvironment&, int32>)
			{
				ShaderType::modifyCompileEnvironment(o, PermutationId);
			}
			else
			{
				static_assert(requires { typename ShaderType::Super; });
				modifyCompileEnvironment<ShaderType::Super>(o, PermutationId);
			}
		}

		template<typename ShaderType>
		bool shouldCompilePermutation(int32 PermutationId)
		{
			if constexpr (std::is_invocable_v<decltype(&ShaderType::shouldCompilePermutation), int32>)
			{
				static_assert(std::is_same_v<std::invoke_result_t<decltype(&ShaderType::shouldCompilePermutation), int32>, bool>);
				return ShaderType::shouldCompilePermutation(PermutationId);
			}
			else
			{
				static_assert(requires { typename ShaderType::Super; });
				return shouldCompilePermutation<ShaderType::Super>(PermutationId);
			}
		}
	public:
		explicit ShaderPermutationBatchCompile(const GlobalShaderRegisteredInfo& info)
			: m_registedInfo(info)
		{

		}

		// Add one shader permutation.
		template<typename ShaderType, typename Permutation>
		auto& add(const Permutation& permutation)
		{
			int32 permutationId = permutation.toId();
			if (shouldCompilePermutation<ShaderType>(permutationId))
			{
				ShaderPermutationCompileInfo info(m_registedInfo, permutationId);

				// Shader level modified environment.
				modifyCompileEnvironment<ShaderType>(info.env, permutationId);

				// Permutation level modified compile environment.
				permutation.modifyCompileEnvironment(info.env);

				// Add batch.
				m_batches.push_back(std::move(info));
			}

			return *this;
		}

		// Add one default shader.
		template <typename ShaderType>
		auto& addDummy()
		{
			ShaderPermutationCompileInfo info(m_registedInfo, 0);

			// Shader level modified environment.
			modifyCompileEnvironment<ShaderType>(info.env, 0);

			// Add batch.
			m_batches.push_back(std::move(info));

			return *this;
		}

		const auto& getBatches() const
		{
			return m_batches;
		}

		const auto& getRegisteredInfo() const
		{
			return m_registedInfo;
		}

		void updateBatchesHash()
		{
			for (auto& batch : m_batches)
			{
				batch.updateHash();
			}
		}

		const GlobalShaderRegisteredInfo& getMetaInfo() const
		{
			return m_registedInfo;
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
		static void modifyCompileEnvironment(ShaderCompileEnvironment& o, int32 PermutationId) { }

		// Can used to discard some impossible permutation composite.
		static bool shouldCompilePermutation(int32 PermutationId) { return true; }
	};



	class GlobalShaderRegisterTable : NonCopyable
	{
		template<class ShaderType>
		friend class GlobalShaderRegister;

	public:
		static GlobalShaderRegisterTable& get();

		const auto& getTable() const { return m_registeredShaders; }
		const auto& getBatchCompile() const { return m_batchCompile; }

		auto& getBatchCompile() { return m_batchCompile; }

	private:
		GlobalShaderRegisterTable() = default;
		
		std::unordered_map<size_t, std::unique_ptr<GlobalShaderRegisteredInfo>> m_registeredShaders;
		std::vector<ShaderPermutationBatchCompile> m_batchCompile;
	};

	template<class ShaderType>
	class GlobalShaderRegister : NonCopyable
	{
	public:
		explicit GlobalShaderRegister(const char* name, const char* file, const char* entry, EShaderStage stage)
		{
			auto& table   = GlobalShaderRegisterTable::get().m_registeredShaders;
			auto& batches = GlobalShaderRegisterTable::get().m_batchCompile;

			const auto& typeHash = getTypeHash<ShaderType>();
			check(table[typeHash] == nullptr);

			// Register global shader meta info.
			table[typeHash] = std::make_unique<GlobalShaderRegisteredInfo>(name, file, entry, stage);
			const auto& ref = *table[typeHash];

			// Fetch all shader permutations.
			if constexpr (requires { typename ShaderType::Permutation; })
			{
				using Permutation = ShaderType::Permutation;
				constexpr uint32 permutationCount = Permutation::kCount;

				ShaderPermutationBatchCompile batch(ref);
				for (auto i = 0; i < permutationCount; i++)
				{
					batch.add<ShaderType, Permutation>(Permutation::fromId(i));
				}
				batches.push_back(std::move(batch));
			}
			else
			{
				ShaderPermutationBatchCompile batch(ref);
				batch.addDummy<ShaderType>();

				batches.push_back(std::move(batch));
			}
		}
	};
	#define IMPLEMENT_GLOBAL_SHADER(Type, File, Entry, Stage) static GlobalShaderRegister<Type> chord_hiden_obj_##Type(#Type, File, Entry, Stage);

	// All shader cache library.
	class ShaderLibrary : NonCopyable
	{
	public:
		void init();

		// Get shader file.
		std::shared_ptr<ShaderFile> getShaderFile(const std::string& path);

		// Get shader with permutation.
		template<typename ShaderType, typename PermutationType = int32>
		std::shared_ptr<ShaderModule> getShader(PermutationType permutation = 0)
		{
			const auto& globalShaderTable = GlobalShaderRegisterTable::get().getTable();
			const auto& typeHash = getTypeHash<ShaderType>();

			// Find global shader info.
			const auto& info = *globalShaderTable.at(typeHash);

			// Now get permutation.
			return getShaderFile(info.shaderFilePath)->getShaderModule(info, permutation);
		}

		void tick(const ApplicationTickData& tickData);

	private:
		void release();
		void handleRecompile();

	private:
		std::map<uint64, std::shared_ptr<ShaderFile>> m_shaderFiles;
	};

	class ComputePipelineCreateInfo
	{
	public:
		explicit ComputePipelineCreateInfo(
			uint32 inPushConstSize, 
			VkShaderModule inShaderModule,
			const std::string& inShaderEntryName)
			: pushConstantSize(inPushConstSize)
			, shaderModule(inShaderModule)
			, shaderEntryName(inShaderEntryName)
		{

		}

		uint64 hash() const
		{
			uint64 hash = hashCombine(pushConstantSize, uint64(shaderModule));

			return hash;
		}

		const VkShaderModule shaderModule;
		const uint32 pushConstantSize;
		const std::string shaderEntryName;
	};

	class GraphicsPipelineCreateInfo
	{
	public:
		// Hide construct function, only compute this pipeline ci from build function.
		explicit GraphicsPipelineCreateInfo(
			std::vector<PipelineShaderStageCreateInfo>&& stages, 
			std::vector<VkFormat>&& attachments, 
			uint32 pushConstSize,
			VkFormat inDepthFormat,
			VkFormat inStencilFormat,
			VkPrimitiveTopology inTopology,
			VkShaderStageFlags inShaderStageFlags)
			: pipelineStages(stages)
			, attachmentFormats(attachments)
			, pushConstantSize(pushConstSize)
			, depthFormat(inDepthFormat)
			, stencilFormat(inStencilFormat)
			, topology(inTopology)
			, shaderStageFlags(inShaderStageFlags)
		{

		}

		uint64 hash() const
		{
			uint64 hash = hashCombine(pushConstantSize, uint64(topology));
				   hash = hashCombine(hash, uint64(depthFormat));
				   hash = hashCombine(hash, uint64(stencilFormat));

			for (auto& format : attachmentFormats)
			{
				hash = hashCombine(hash, uint64(format));
			}
			for (auto& stage : pipelineStages)
			{
				hash = hashCombine(hash, stage.hash());
			}

			return hash;
		}

		// Member of the structure, remember add hash combine in hash function.
		const std::vector<PipelineShaderStageCreateInfo> pipelineStages;
		const std::vector<VkFormat> attachmentFormats;
		const VkShaderStageFlags shaderStageFlags;
		const uint32 pushConstantSize;
		const VkFormat depthFormat;
		const VkFormat stencilFormat;
		const VkPrimitiveTopology topology;
	};

	template<class MeshShader, class PixelShader>
	GraphicsPipelineRef Context::graphicsMeshPipe(
		const std::string& name, 
		std::vector<VkFormat>&& attachments, 
		VkFormat inDepthFormat, 
		VkFormat inStencilFormat, 
		VkPrimitiveTopology inTopology)
	{
		auto meshShader = getShaderLibrary().getShader<MeshShader>();
		auto pixelShader = getShaderLibrary().getShader<PixelShader>();

		return graphicsMeshShadingPipe(nullptr, meshShader, pixelShader, name, std::move(attachments), inDepthFormat, inStencilFormat, inTopology);
	}

	template<class ComputeShader>
	inline ComputePipelineRef Context::computePipe(const std::string& name)
	{
		auto computeShader = getShaderLibrary().getShader<ComputeShader>();
		return computePipe(computeShader, name);
	}

	template<class AmplifyShader, class MeshShader, class PixelShader>
	GraphicsPipelineRef Context::graphicsAmplifyMeshPipe(
		const std::string& name, 
		std::vector<VkFormat>&& attachments,
		VkFormat inDepthFormat, 
		VkFormat inStencilFormat, 
		VkPrimitiveTopology inTopology)
	{
		auto taskShader  = getShaderLibrary().getShader<AmplifyShader>();
		auto meshShader  = getShaderLibrary().getShader<MeshShader>();
		auto pixelShader = getShaderLibrary().getShader<PixelShader>();

		return graphicsMeshShadingPipe(taskShader, meshShader, pixelShader, name, std::move(attachments), inDepthFormat, inStencilFormat, inTopology);
	}

	GraphicsPipelineRef Context::graphicsMeshShadingPipe(
		std::shared_ptr<ShaderModule> amplifyShader,
		std::shared_ptr<ShaderModule> meshShader,
		std::shared_ptr<ShaderModule> pixelShader,
		const std::string& name,
		std::vector<VkFormat>&& attachments,
		VkFormat inDepthFormat,
		VkFormat inStencilFormat,
		VkPrimitiveTopology inTopology)
	{
		uint32 pushConstantSize = 0;
		if (amplifyShader != nullptr)
		{
			pushConstantSize = amplifyShader->getPushConstSize();
		}

		pushConstantSize = math::max(math::max(pushConstantSize, meshShader->getPushConstSize()), pixelShader->getPushConstSize());

		std::vector<PipelineShaderStageCreateInfo> cis = { };
		if (amplifyShader != nullptr)
		{
			cis.push_back(amplifyShader->getShaderStageCreateInfo());
		}
		cis.push_back(meshShader->getShaderStageCreateInfo());
		cis.push_back(pixelShader->getShaderStageCreateInfo());

		constexpr VkShaderStageFlags fullFlags   = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;
		constexpr VkShaderStageFlags noTaskFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;

		const auto graphicsCi = GraphicsPipelineCreateInfo(
			std::move(cis),
			std::move(attachments),
			pushConstantSize,
			inDepthFormat,
			inStencilFormat,
			inTopology,
			amplifyShader != nullptr ? fullFlags : noTaskFlags);

		return getPipelineContainer().graphics(name, graphicsCi);
	}

	ComputePipelineRef Context::computePipe(
		std::shared_ptr<ShaderModule> computeShader,
		const std::string& name)
	{
		uint32 pushConstSize = computeShader->getPushConstSize();
		auto pipelineCI = computeShader->getShaderStageCreateInfo();

		const auto computeCI = ComputePipelineCreateInfo(pushConstSize, pipelineCI.module, pipelineCI.pName);
		return getPipelineContainer().compute(name, computeCI);
	}


	template<class VertexShader, class PixelShader>
	GraphicsPipelineRef Context::graphicsPipe(
		const std::string& name, 
		std::vector<VkFormat>&& attachments, 
		VkFormat inDepthFormat, 
		VkFormat inStencilFormat,
		VkPrimitiveTopology inTopology)
	{
		auto vertexShader = getShaderLibrary().getShader<VertexShader>();
		auto pixelShader  = getShaderLibrary().getShader<PixelShader>();

		// We always share push const, so should use max size.
		const uint32 pushConstantSize = math::max(vertexShader->getPushConstSize(), pixelShader->getPushConstSize());

		const auto graphicsCi = GraphicsPipelineCreateInfo(
			{ vertexShader->getShaderStageCreateInfo(), pixelShader->getShaderStageCreateInfo() },
			std::move(attachments),
			pushConstantSize,
			inDepthFormat,
			inStencilFormat,
			inTopology,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

		return getPipelineContainer().graphics(name, graphicsCi);
	}


	#define DECLARE_GLOBAL_SHADER(ShaderName) class ShaderName : public graphics::GlobalShader { public: DECLARE_SUPER_TYPE(graphics::GlobalShader); }
	#define PRIVATE_GLOBAL_SHADER(ShaderName, Path, Entry, Stage) \
		DECLARE_GLOBAL_SHADER(ShaderName);    \
		IMPLEMENT_GLOBAL_SHADER(ShaderName, Path, Entry, Stage)
}