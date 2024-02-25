#pragma once

#include <utils/log.h>
#include <utils/subsystem.h>

namespace chord::graphics
{
	// Cache GPU queue infos.
	struct GPUQueuesInfo
	{
		OptionalUint32 graphicsFamily      { };
		OptionalUint32 computeFamily       { };
		OptionalUint32 copyFamily          { };
		OptionalUint32 sparseBindingFamily { };
		OptionalUint32 videoDecodeFamily   { };

		struct Queue
		{
			VkQueue queue;
			float priority;
		};

		// Sort by priority, descending order.
		std::vector<Queue> computeQueues;  
		std::vector<Queue> copyQueues;     
		std::vector<Queue> graphcisQueues; 
		std::vector<Queue> spatialBindingQueues;
		std::vector<Queue> videoDecodeQueues;
	};

	namespace log
	{
		extern spdlog::logger& get();
	}
}

#ifdef ENABLE_LOG
	#define LOG_GRAPHICS_TRACE(...) { ::chord::graphics::log::get().trace   (__VA_ARGS__); }
	#define LOG_GRAPHICS_INFO(...)  { ::chord::graphics::log::get().info    (__VA_ARGS__); }
	#define LOG_GRAPHICS_WARN(...)  { ::chord::graphics::log::get().warn    (__VA_ARGS__); }
	#define LOG_GRAPHICS_ERROR(...) { ::chord::graphics::log::get().error   (__VA_ARGS__); }
	#define LOG_GRAPHICS_FATAL(...) { ::chord::graphics::log::get().critical(__VA_ARGS__); ::chord::applicationCrash(); }
#else
	#define LOG_GRAPHICS_TRACE(...)   
	#define LOG_GRAPHICS_INFO(...)    
	#define LOG_GRAPHICS_WARN(...)   
	#define LOG_GRAPHICS_ERROR(...)    
	#define LOG_GRAPHICS_FATAL(...) { ::chord::applicationCrash(); }
#endif

#if CHORD_DEBUG
	#define CHECK_GRAPHICS(x) { if(!(x)) { LOG_GRAPHICS_FATAL("Check error, at line {0} on file {1}.", __LINE__, __FILE__); DEBUG_BREAK(); } }
	#define ASSERT_GRAPHICS(x, ...) { if(!(x)) { LOG_GRAPHICS_FATAL("Assert failed: '{2}', at line {0} on file {1}.", __LINE__, __FILE__, std::format(__VA_ARGS__)); DEBUG_BREAK(); } }
#else
	#define CHECK_GRAPHICS(x) { if(!(x)) { LOG_GRAPHICS_FATAL("Check error."); } }
	#define ASSERT_GRAPHICS(x, ...) { if(!(x)) { LOG_GRAPHICS_FATAL("Assert failed: {0}.", __VA_ARGS__); } }
#endif

#define ENSURE_GRAPHICS(x, ...) { static bool bExecuted = false; if((!bExecuted) && !(x)) { bExecuted = true; LOG_GRAPHICS_ERROR("Ensure failed in graphics: '{2}', at line {0} on file {1}.", __LINE__, __FILE__, std::format(__VA_ARGS__)); DEBUG_BREAK(); } }

#define ChordVkGetNextPtr(v) &(v).pNext
#define ChordVkStepNextPtr(ptr, v) (*(ptr)) = &(v); (ptr) = &((v).pNext);

namespace chord::graphics
{
	static inline void checkVkResult(VkResult result)
	{
		CHECK_GRAPHICS(result == VK_SUCCESS);
	}

	class Context : public ISubsystem
	{
	public:
		struct InitConfig
		{
			// Instance layers.
			bool bInstanceLayerValidation = false;

			// Instance extension.
			bool bInstanceExtensionDebugUtils = false;
			bool bInstanceExtensionGLFW = false;

			// Device extensions.
			bool b8BitStorage   = false;
			bool b16BitStorage  = false;

			// Raytracing feature.
			bool bRaytracing = false;

			// Enable when application will create windows.
			bool bHDR = false;
			bool bSwapchain = false;
		};

		explicit Context(const InitConfig& config)
			: ISubsystem("Graphics")
			, m_initConfig(config)
		{

		}

		struct PhysicalDeviceProperties
		{
			VkPhysicalDeviceMemoryProperties   memoryProperties;
			VkPhysicalDeviceProperties         deviceProperties;
			VkPhysicalDeviceProperties2        deviceProperties2;
			VkPhysicalDeviceSubgroupProperties subgroupProperties;
			VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties;

			// KHR.
			VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties;
		};

	protected:
		virtual void beforeRelease() override;
		virtual bool onInit() override;
		virtual bool onTick(const SubsystemTickData& tickData) override;
		virtual void onRelease() override;

	private:
		InitConfig m_initConfig = { };

		// Vulkan instance.
		VkInstance m_instance = VK_NULL_HANDLE;

		// Vulkan debug utils handle.
		VkDebugUtilsMessengerEXT m_debugUtilsHandle = VK_NULL_HANDLE;

		// Vulkan logic device.
		VkDevice m_device = VK_NULL_HANDLE;

		// Vulkan physical device.
		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

		// Vulkan cached physical device's properties.
		PhysicalDeviceProperties m_physicalDeviceProperties;

		GPUQueuesInfo m_gpuQueuesInfo;
	};
}

namespace chord
{
	extern graphics::Context& getContext();
}