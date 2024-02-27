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

	#define LOG_GRAPHICS_TRACE(...) chord_macro_sup_enableLogOnly({ log::get().trace(__VA_ARGS__); })
	#define LOG_GRAPHICS_INFO(...) chord_macro_sup_enableLogOnly({ log::get().info(__VA_ARGS__); })
	#define LOG_GRAPHICS_WARN(...) chord_macro_sup_enableLogOnly({ log::get().warn(__VA_ARGS__); })
	#define LOG_GRAPHICS_ERROR(...) chord_macro_sup_enableLogOnly({ log::get().error(__VA_ARGS__); })
	#define LOG_GRAPHICS_FATAL(...) chord_macro_sup_enableLogOnly({ log::get().critical(__VA_ARGS__); ::chord::applicationCrash(); })
}

#define checkGraphics(x) chord_macro_sup_checkPrintContent(x, LOG_GRAPHICS_FATAL)
#define checkGraphicsMsgf(x, ...) chord_macro_sup_checkMsgfPrintContent(x, LOG_GRAPHICS_FATAL, __VA_ARGS__)
#define ensureGraphicsMsgf(x, ...) chord_macro_sup_ensureMsgfContent(x, LOG_GRAPHICS_ERROR, __VA_ARGS__)
#define checkVkResult(x) checkGraphics((x) == VK_SUCCESS)

namespace chord::graphics
{
	static inline auto getNextPtr(auto& v)
	{
		return &v.pNext;
	}

	static inline void stepNextPtr(auto& ptr, auto& v)
	{
		*(ptr) = &(v);
		 (ptr) = &(v.pNext);
	}

	class Context : public ISubsystem
	{
	public:
		struct InitConfig
		{
			bool bValidation  = false;
			bool bDebugUtils  = false;
			bool bGLFW        = false;
			bool bHDR         = false;
			bool bRaytracing  = false;
		};



		explicit Context(const InitConfig& config)
			: ISubsystem("Graphics")
			, m_initConfig(config)
		{

		}

		struct PhysicalDeviceProperties
		{
			VkPhysicalDeviceMemoryProperties   memoryProperties   { };
			VkPhysicalDeviceProperties         deviceProperties   { };
			VkPhysicalDeviceProperties2        deviceProperties2  { };
			VkPhysicalDeviceSubgroupProperties subgroupProperties { };
			VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties { };

			// KHR.
			VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties { };
		};

		struct PhysicalDeviceFeatures
		{
			// Core.
			VkPhysicalDeviceFeatures core10Features
			{
			
			};
			VkPhysicalDeviceVulkan11Features core11Features
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
			};
			VkPhysicalDeviceVulkan12Features core12Features
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
			};
			VkPhysicalDeviceVulkan13Features core13Features
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
			};

			// KHR.
			VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR
			};
			VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingPipelineFeatures
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR
			};
			VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR
			};

			// EXT.
			VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT
			};
			VkPhysicalDeviceExtendedDynamicState2FeaturesEXT extendedDynamicState2Features
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT
			};

			// Update pNext chain when query or create device.
			void** stepNextPtr(auto& s)
			{
				auto pNext = getNextPtr(s);

				graphics::stepNextPtr(pNext, this->core11Features);
				graphics::stepNextPtr(pNext, this->core12Features);
				graphics::stepNextPtr(pNext, this->core13Features);
				graphics::stepNextPtr(pNext, this->rayQueryFeatures);
				graphics::stepNextPtr(pNext, this->raytracingPipelineFeatures);
				graphics::stepNextPtr(pNext, this->accelerationStructureFeatures);
				graphics::stepNextPtr(pNext, this->extendedDynamicState2Features);
				graphics::stepNextPtr(pNext, this->extendedDynamicState3Features);

				return pNext;
			}
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
		PhysicalDeviceProperties m_physicalDeviceProperties = { };

		// Vulkan cached physical device features.
		PhysicalDeviceFeatures m_physicalDeviceFeatures = { };

		// Current enable physical device features.
		PhysicalDeviceFeatures m_physicalDeviceFeaturesEnabled = { };

		// Vulkan gpu queue infos.
		GPUQueuesInfo m_gpuQueuesInfo = { };
	};
}

namespace chord
{
	extern graphics::Context& getContext();
}