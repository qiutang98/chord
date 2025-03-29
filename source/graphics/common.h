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
	class GPUGLTFPrimitiveAsset;
}

namespace chord::graphics
{
	constexpr uint32 kMaxRenderTargets =  8U;

	enum class ERenderTargetLoadStoreOp : uint8
	{
		Clear_Store,
		Load_Store,
		Nope_Store,

		Clear_Nope,
		Load_Nope,
		Nope_Nope,
	};

	static inline VkAttachmentLoadOp getAttachmentLoadOp(ERenderTargetLoadStoreOp op)
	{
		if (op == ERenderTargetLoadStoreOp::Clear_Store ||
			op == ERenderTargetLoadStoreOp::Clear_Nope)
		{
			return VK_ATTACHMENT_LOAD_OP_CLEAR;
		}

		if (op == ERenderTargetLoadStoreOp::Load_Store ||
			op == ERenderTargetLoadStoreOp::Load_Nope)
		{
			return VK_ATTACHMENT_LOAD_OP_LOAD;
		}

		if (op == ERenderTargetLoadStoreOp::Nope_Store ||
			op == ERenderTargetLoadStoreOp::Nope_Nope)
		{
			return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		}

		checkEntry();
		return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}

	static inline VkAttachmentStoreOp getAttachmentStoreOp(ERenderTargetLoadStoreOp op)
	{
		if (op == ERenderTargetLoadStoreOp::Clear_Store ||
			op == ERenderTargetLoadStoreOp::Load_Store  ||
			op == ERenderTargetLoadStoreOp::Nope_Store)
		{
			return VK_ATTACHMENT_STORE_OP_STORE;
		}

		if (op == ERenderTargetLoadStoreOp::Clear_Nope ||
			op == ERenderTargetLoadStoreOp::Load_Nope ||
			op == ERenderTargetLoadStoreOp::Nope_Nope)
		{
			return VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}

		checkEntry();
		return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}

	enum class EDepthStencilOp : uint8
	{
		DepthWrite   = 0x01 << 0,
		DepthRead    = 0x01 << 1,
		StencilWrite = 0x01 << 2,
		StnecilRead  = 0x01 << 3,

		DepthWrite_StencilWrite = DepthWrite | StencilWrite,
		DepthWrite_StnecilRead  = DepthWrite | StnecilRead,

		DepthRead_StencilWrite  = DepthRead | StencilWrite,
		DepthRead_StnecilRead   = DepthRead | StnecilRead,
	};
	ENUM_CLASS_FLAG_OPERATORS(EDepthStencilOp);

	static inline VkImageLayout getLayoutFromDepthStencilOp(EDepthStencilOp op, bool bExistStencilComponent)
	{
		if (bExistStencilComponent)
		{
			if (op == EDepthStencilOp::DepthWrite_StencilWrite) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			if (op == EDepthStencilOp::DepthWrite_StnecilRead)  return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
			if (op == EDepthStencilOp::DepthRead_StencilWrite)  return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
			if (op == EDepthStencilOp::DepthRead_StnecilRead)   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}
		else
		{
			if (op == EDepthStencilOp::DepthWrite) return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
			if (op == EDepthStencilOp::DepthRead)  return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
		}

		checkEntry();
		return VK_IMAGE_LAYOUT_UNDEFINED;
	}

	static inline VkAccessFlagBits getAccessFlagBits(EDepthStencilOp op)
	{
		switch (op)
		{
		case EDepthStencilOp::DepthWrite:
		case EDepthStencilOp::DepthWrite_StencilWrite:
		case EDepthStencilOp::DepthWrite_StnecilRead:
		case EDepthStencilOp::DepthRead_StencilWrite:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		case EDepthStencilOp::DepthRead:
		case EDepthStencilOp::DepthRead_StnecilRead:
			return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		}

		checkEntry();
		return VK_ACCESS_NONE;
	}

	static inline bool isFormatExistStencilComponent(VkFormat format)
	{
		return format == VK_FORMAT_D32_SFLOAT_S8_UINT 
			|| format == VK_FORMAT_D16_UNORM_S8_UINT
			|| format == VK_FORMAT_D24_UNORM_S8_UINT;
	}

	class ImageView;
	class ShaderCompilerManager;
	class ShaderLibrary;
	class GPUTexturePool;
	class GPUBufferPool;

	using ShaderCompileArguments = std::vector<std::string>;
	class ShaderModule;
	class AsyncUploaderManager;

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

	static inline SamplerCreateInfo buildBasicSamplerCreateInfo()
	{
		SamplerCreateInfo info { };
		info.magFilter  = VK_FILTER_NEAREST;
		info.minFilter  = VK_FILTER_NEAREST;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

		info.minLod     = -10000.0f;
		info.maxLod     = 10000.0f;
		info.mipLodBias = 0.0f;

		info.anisotropyEnable        = VK_FALSE;
		info.compareEnable           = VK_FALSE;
		info.unnormalizedCoordinates = VK_FALSE;

		return info;
	}

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

		explicit CommandPoolResetable(const std::string& name, uint32 family);
		~CommandPoolResetable();
	};

	class GPUTimestamps
	{
	public:
		struct TimeStamp
		{
			std::string label;
			float microseconds;
		};

		void init(uint32 numberOfBackBuffers, uint32 maxTimeStampCounts);
		void release();

		void getTimeStamp(VkCommandBuffer cmd, const char* label);
		void getTimeStampCPU(TimeStamp ts); 

		void onBeginFrame(VkCommandBuffer cmd, std::vector<TimeStamp>* pTimestamp);
		void onEndFrame();

	private:
		VkQueryPool m_queryPool;

		uint32 m_maxTimeStampCount = 0;
		uint32 m_frame = 0;
		uint32 m_numberOfBackBuffers = 0;

		std::vector<std::string> m_labels[5];
		std::vector<TimeStamp> m_cpuTimeStamps[5];
	};

	namespace logger
	{
		extern spdlog::logger& get();
	}

	#define LOG_GRAPHICS_TRACE(...) chord_macro_sup_enableLogOnly({ logger::get().trace(__VA_ARGS__); })
	#define LOG_GRAPHICS_INFO(...) chord_macro_sup_enableLogOnly({ logger::get().info(__VA_ARGS__); })
	#define LOG_GRAPHICS_WARN(...) chord_macro_sup_enableLogOnly({ logger::get().warn(__VA_ARGS__); })
	#define LOG_GRAPHICS_ERROR(...) chord_macro_sup_enableLogOnly({ logger::get().error(__VA_ARGS__); })
	#define LOG_GRAPHICS_FATAL(...) chord_macro_sup_enableLogOnly({ logger::get().critical(__VA_ARGS__); CHORD_CRASH })

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
