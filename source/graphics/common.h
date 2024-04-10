#pragma once

#include <utils/log.h>
#include <utils/subsystem.h>
#include <utils/delegate.h>
#include <utils/crc.h>
#include <utils/cvar.h>

namespace chord
{
	class ApplicationTickData;
	class ImGuiManager;
}

namespace chord::graphics
{
	class ImageView;
	class ShaderCompilerManager;
	class ShaderLibrary;
	class GPUTexturePool;
	class GPUBufferPool;

	using ShaderCompileArguments = std::vector<std::string>;
	class ShaderModule;

	// Shader stage.
	enum class EShaderStage
	{
		Vertex,
		Pixel,
		Compute,
		Mesh,
		Amplify,

		MAX
	};

	enum class EQueueType
	{
		Graphics,
		Compute,
		Copy,

		MAX
	};

	// Cache GPU queue infos.
	struct GPUQueuesInfo
	{
		OptionalUint32 graphicsFamily{ };
		OptionalUint32 computeFamily{ };
		OptionalUint32 copyFamily{ };
		OptionalUint32 sparseBindingFamily{ };
		OptionalUint32 videoDecodeFamily{ };
		OptionalUint32 videoEncodeFamily{ };

		struct Queue
		{
			// Vulkan queue.
			VkQueue queue;

			// Current queue priority.
			float priority;
		};

		// Sort by priority, descending order.
		std::vector<Queue> computeQueues;
		std::vector<Queue> copyQueues;
		std::vector<Queue> graphcisQueues;
		std::vector<Queue> spatialBindingQueues;
		std::vector<Queue> videoDecodeQueues;
		std::vector<Queue> videoEncodeQueues;
	};

	// Trait create info type to support memory hash.
	struct PipelineShaderStageCreateInfo
	{
		uint64 hash() const
		{
			uint64 hash = std::hash<const char*>{}(pName);
			hash = hashCombine(hash, uint64(module));
			hash = hashCombine(hash, uint64(flags));
			hash = hashCombine(hash, uint64(stage));

			return hash;
		}

		const char* pName;
		VkShaderModule module;
		VkPipelineShaderStageCreateFlags flags;
		VkShaderStageFlagBits stage;
	};

	// Hash by memory hash.
	struct SamplerCreateInfo
	{
		VkSamplerCreateFlags flags;
		VkFilter             magFilter;
		VkFilter             minFilter;
		VkSamplerMipmapMode  mipmapMode;
		VkSamplerAddressMode addressModeU;
		VkSamplerAddressMode addressModeV;
		VkSamplerAddressMode addressModeW;
		float                mipLodBias;
		VkBool32             anisotropyEnable;
		float                maxAnisotropy;
		VkBool32             compareEnable;
		VkCompareOp          compareOp;
		float                minLod;
		float                maxLod;
		VkBorderColor        borderColor;
		VkBool32             unnormalizedCoordinates;
	};

	class PipelineLayoutManager : NonCopyable
	{
	public:
		~PipelineLayoutManager();

		VkPipelineLayout getLayout(
			uint32 setLayoutCount,
			const VkDescriptorSetLayout* pSetLayouts,
			uint32 pushConstantRangeCount,
			const VkPushConstantRange* pPushConstantRanges);

	private:
		void clear();

	private:
		std::map<uint64, OptionalVkPipelineLayout> m_layouts;
	};

	class CommandPoolResetable : NonCopyable
	{
	private:
		VkCommandPool m_pool = VK_NULL_HANDLE;
		uint32 m_family;

	public:
		VkCommandPool pool() const { return m_pool; }
		uint32 family() const { return m_family; }

		explicit CommandPoolResetable(const std::string& name);
		~CommandPoolResetable();
	};

	namespace log
	{
		extern spdlog::logger& get();
	}

	#define LOG_GRAPHICS_TRACE(...) chord_macro_sup_enableLogOnly({ log::get().trace(__VA_ARGS__); })
	#define LOG_GRAPHICS_INFO(...) chord_macro_sup_enableLogOnly({ log::get().info(__VA_ARGS__); })
	#define LOG_GRAPHICS_WARN(...) chord_macro_sup_enableLogOnly({ log::get().warn(__VA_ARGS__); })
	#define LOG_GRAPHICS_ERROR(...) chord_macro_sup_enableLogOnly({ log::get().error(__VA_ARGS__); })
	#define LOG_GRAPHICS_FATAL(...) chord_macro_sup_enableLogOnly({ log::get().critical(__VA_ARGS__); ::chord::applicationCrash(); })

	static inline auto getNextPtr(auto& v)
	{
		return &v.pNext;
	}

	static inline void stepNextPtr(auto& ptr, auto& v)
	{
		*(ptr) = &(v);
		(ptr) = &(v.pNext);
	}
}

#define checkGraphics(x) chord_macro_sup_checkPrintContent(x, LOG_GRAPHICS_FATAL)
#define checkGraphicsMsgf(x, ...) chord_macro_sup_checkMsgfPrintContent(x, LOG_GRAPHICS_FATAL, __VA_ARGS__)
#define ensureGraphicsMsgf(x, ...) chord_macro_sup_ensureMsgfContent(x, LOG_GRAPHICS_ERROR, __VA_ARGS__)
#define checkVkResult(x) checkGraphics((x) == VK_SUCCESS)
