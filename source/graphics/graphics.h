#pragma once

#include <utils/log.h>
#include <utils/subsystem.h>
#include <utils/delegate.h>

namespace chord
{
	class Window;
}

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
		OptionalUint32 videoEncodeFamily   { };

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
		std::vector<Queue> videoEncodeQueues;
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

namespace chord::graphics
{
	class Swapchain : NonCopyable
	{
	public:
		enum class EFormatType
		{
			None = 0,
			sRGB10Bit,
			sRGB8Bit,
			scRGB,
			ST2084,

			MAX
		};

		struct SupportDetails
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> surfaceFormats;
			std::vector<VkPresentModeKHR> presentModes;
		};

		explicit Swapchain();
		~Swapchain();

		VkSwapchainKHR getSwapchain() const 
		{ 
			return m_swapchain; 
		}

		const VkExtent2D& getExtent() const
		{
			return m_extent;
		}

		EFormatType getFormatType() const
		{
			return m_formatType;
		}

		uint32 getBackbufferCount() const
		{
			return m_backbufferCount;
		}

		// Acquire next present image, return using image index.
		uint32 acquireNextPresentImage();

		void markDirty() { m_bSwapchainChange = true; }

		void submit(uint32 count, VkSubmitInfo* infos);
		void present();

		VkSemaphore getCurrentFrameWaitSemaphore() const;
		VkSemaphore getCurrentFrameFinishSemaphore() const;

		std::pair<VkCommandBuffer, VkSemaphore> beginFrameCmd(uint64 tickCount) const;

		// Param #0: old dimension, param #1: new dimension.
		Events<Swapchain> onBeforeSwapchainRecreate;

		// Param #0: old dimension, param #1: new dimension.
		Events<Swapchain> onAfterSwapchainRecreate;

	private:
		void createContext();
		void releaseContext();

		// Context recreate.
		void recreateContext();

	private:
		// Working queue.
		VkQueue m_queue;

		// Vulkan surface.
		VkSurfaceKHR m_surface = VK_NULL_HANDLE;

		// Vulkan back buffer format type.
		EFormatType m_formatType = EFormatType::None;

		// Vulkan surface format.
		VkSurfaceFormatKHR m_surfaceFormat = {};

		// Current swapchain support detail.
		SupportDetails m_supportDetails;

		// Window swapchain.
		VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;

		// Own image.
		std::vector<VkImage> m_swapchainImages = {};

		// Image views.
		std::vector<VkImageView> m_swapchainImageViews = {};

		// Extent of swapchain.
		VkExtent2D m_extent = {};

		// Current swapchain present mode.
		VkPresentModeKHR m_presentMode = {};

		// Ring present realtive info.
		uint32 m_backbufferCount = 0;
		uint32 m_imageIndex = 0;
		uint32 m_currentFrame = 0;

		// Semaphores.
		std::vector<VkSemaphore> m_semaphoresImageAvailable;
		std::vector<VkSemaphore> m_semaphoresRenderFinished;

		// Fences.
		std::vector<VkFence> m_inFlightFences;
		std::vector<VkFence> m_imagesInFlight;

		// Swapchain dirty need rebuild context.
		bool m_bSwapchainChange = false;

		// Generic command pool for renderer, commonly is ring style.
		struct RendererCommandPool
		{
			uint32 family = ~0;
			VkQueue queue = VK_NULL_HANDLE;
			VkCommandPool pool = VK_NULL_HANDLE;
		};
		RendererCommandPool m_cmdPool;
		std::vector<VkCommandBuffer> m_cmdBufferRing;
		std::vector<VkSemaphore> m_cmdSemaphoreRing; // Semaphore wait for cmd buffer ring.
	};

	class Context : public ISubsystem
	{
	public:
		struct InitConfig
		{
			bool bValidation  = false;
			bool bDebugUtils  = false;
			bool bGLFW        = false; // GLFW support.
			bool bHDR         = false; // HDR features support.
			bool bRaytracing  = false; // Ray tracing support.

			// Vulkan allocation callbacks.
			VkAllocationCallbacks* pAllocationCallbacks = nullptr;
		};

		explicit Context(const InitConfig& config);

		struct PhysicalDeviceProperties
		{
			VkPhysicalDeviceMemoryProperties memoryProperties {};
			VkPhysicalDeviceProperties deviceProperties { };
			VkPhysicalDeviceProperties2 deviceProperties2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
			VkPhysicalDeviceSubgroupProperties subgroupProperties { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
			VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES };

			// KHR.
			VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
		
			void** getNextPtr()
			{
				auto pNext = graphics::getNextPtr(this->deviceProperties2);

				graphics::stepNextPtr(pNext, this->subgroupProperties);
				graphics::stepNextPtr(pNext, this->descriptorIndexingProperties);
				graphics::stepNextPtr(pNext, this->accelerationStructureProperties);

				return pNext;
			}
		};

		struct PhysicalDeviceFeatures
		{
			// Core.
			VkPhysicalDeviceFeatures core10Features {};
			VkPhysicalDeviceVulkan11Features core11Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
			VkPhysicalDeviceVulkan12Features core12Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			VkPhysicalDeviceVulkan13Features core13Features { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

			// KHR.
			VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures 
			{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
			VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingPipelineFeatures 
			{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
			VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures 
			{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };

			// EXT.
			VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features 
			{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
			VkPhysicalDeviceExtendedDynamicState2FeaturesEXT extendedDynamicState2Features 
			{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT };

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

		VkInstance getInstance() const { return m_instance; }
		VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
		VkDevice getDevice() const { return m_device; }

		// Major graphics queue is most importance queue with highest priority.
		VkQueue getMajorGraphicsQueue() const { return m_gpuQueuesInfo.graphcisQueues[0].queue; }

		// Get graphics queues info.
		const auto& getQueuesInfo() const { return m_gpuQueuesInfo; }

		// Vulkan allocation callbacks, register in init config.
		const auto* getAllocationCallbacks() const { return m_initConfig.pAllocationCallbacks; }

		// Current graphics support HDR.
		bool isRequiredHDR() const { return m_initConfig.bHDR; }

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

		// Vulkan pipeline cache.
		VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;

		// Vulkan swapchain.
		std::unique_ptr<Swapchain> m_swapchain = nullptr;

	};
}

namespace chord::graphics
{
	extern Context& getContext();
	inline VkDevice getDevice() { return getContext().getDevice(); }
	inline const auto* getAllocationCallbacks() { return getContext().getAllocationCallbacks(); }
}