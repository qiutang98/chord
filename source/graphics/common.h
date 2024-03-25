#pragma once

#include <utils/log.h>
#include <utils/subsystem.h>
#include <utils/delegate.h>
#include <utils/crc.h>
#include <utils/cvar.h>

namespace chord::graphics
{
	class ImageView;
	class ShaderCompilerManager;
	class ShaderLibrary;

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
