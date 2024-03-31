#pragma once

#include <graphics/common.h>
#include <graphics/resource.h>
#include <graphics/swapchain.h>
#include <graphics/bindless.h>
#include <graphics/pipeline.h>

namespace chord
{
	class ApplicationTickData;
	class ImGuiManager;
}

namespace chord::graphics
{
	struct PhysicalDeviceFeatures
	{
		PhysicalDeviceFeatures();

		// Update pNext chain when query or create device.
		void** stepNextPtr(void** ppNext);

		// Core.
		VkPhysicalDeviceFeatures core10Features {};
		VkPhysicalDeviceVulkan11Features core11Features { };
		VkPhysicalDeviceVulkan12Features core12Features { };
		VkPhysicalDeviceVulkan13Features core13Features { };

		// KHR.
		VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR raytracingPipelineFeatures{ };
		VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{ };

		// EXT.
		VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features{ };
		VkPhysicalDeviceExtendedDynamicState2FeaturesEXT extendedDynamicState2Features{ };

		VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vertexInputDynamicStateFeatures { };
	};

	struct PhysicalDeviceProperties
	{
		PhysicalDeviceProperties();
		void** getNextPtr();

		VkPhysicalDeviceMemoryProperties memoryProperties{};
		VkPhysicalDeviceProperties deviceProperties{ };
		VkPhysicalDeviceProperties2 deviceProperties2{ };
		VkPhysicalDeviceSubgroupProperties subgroupProperties{ };
		VkPhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties{ };

		// KHR.
		VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{ };
	};

	class BuiltinTextures
	{
	public:
		GPUTextureRef white = nullptr;       // 1x1 RGBA(255,255,255,255)
		GPUTextureRef transparent = nullptr; // 1x1 RGBA(  0,  0,  0,  0)
		GPUTextureRef black = nullptr;       // 1x1 RGBA(  0,  0,  0,255) 
	};

	class Context : NonCopyable
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

		explicit Context() = default;

		const auto& getPhysicalDeviceDescriptorIndexingProperties() { return m_physicalDeviceProperties.descriptorIndexingProperties;  }
		VkInstance getInstance() const { return m_instance; }
		VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
		VkDevice getDevice() const { return m_device; }
		VkPipelineCache getPipelineCache() const { return m_pipelineCache; }

		// Find memory type from memory.
		OptionalUint32 findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties) const;

		// Major graphics queue is most importance queue with highest priority.
		VkQueue getMajorGraphicsQueue() const { return m_gpuQueuesInfo.graphcisQueues[0].queue; }

		// Get graphics queues info.
		const auto& getQueuesInfo() const { return m_gpuQueuesInfo; }

		// Vulkan allocation callbacks, register in init config.
		const auto* getAllocationCallbacks() const { return m_initConfig.pAllocationCallbacks; }

		// Current graphics support HDR.
		bool isRequiredHDR() const { return m_initConfig.bHDR; }

		// Current application own bindless manager.
		auto& getBindlessManger() { return *m_bindlessManager; }
		const auto& getBindlessManger() const { return *m_bindlessManager; }

		auto& getSwapchain() { return *m_swapchain; }
		const auto& getSwapchain() const { return *m_swapchain; }

		// All samplers managed by SamplerManager, only one instance.
		auto& getSamplerManager() { return *m_samplerManager; }
		const auto& getSamplerManager() const { return *m_samplerManager; }

		// Current application own shader compiler.
		auto& getShaderCompiler() { return *m_shaderCompiler; }
		const auto& getShaderCompiler() const { return *m_shaderCompiler; }

		auto& getShaderLibrary() { return *m_shaderLibrary; }
		const auto& getShaderLibrary() const { return *m_shaderLibrary; }

		// Vulkan memory allocator handle.
		VmaAllocator getVMA() const { return m_vmaAllocator; }

		// Engine built textures.
		const BuiltinTextures& getBuiltinTextures() const { return m_builtinTextures; }

		// Create a stage upload buffer, commonly only used for engine init.
		GPUBufferRef createStageUploadBuffer(const std::string& name, SizedBuffer data);

		// Sync upload texture, only used for engine init, should not call in actually render pipeline.
		void syncUploadTexture(GPUTexture& texture, SizedBuffer data);

		// Sync execute command buffer, only used for engine init, should not call in actually render pipeline.
		void executeImmediately(VkCommandPool commandPool, uint32 family, VkQueue queue, std::function<void(VkCommandBuffer cb, uint32 family, VkQueue queue)>&& func) const;

		// Sync execute command buffer in main graphics queue, only used for engine init, should not call in actually render pipeline.
		void executeImmediatelyMajorGraphics(std::function<void(VkCommandBuffer cb, uint32 family, VkQueue queue)>&& func) const;

		// Builtin graphics command pool with RESET bit.
		const CommandPoolResetable& getGraphicsCommandPool() const { return *m_graphicsCommandPool; }

		const auto& getPipelineLayoutManager() const { return *m_pipelineLayoutManager; }
		auto& getPipelineLayoutManager() { return *m_pipelineLayoutManager; }

		const auto& getPipelineContainer() const { return *m_pipelineContainer; }
		auto& getPipelineContainer() { return *m_pipelineContainer; }

		void waitDeviceIdle() const;

	public:
		bool init(const InitConfig& config);
		bool tick(const ApplicationTickData& tickData);

		void beforeRelease();
		void release();

		// After context basic init will call and clean this event.
		Events<Context> onInit;

	private:
		// Sync init builtin textures.
		void initBuiltinTextures();

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

		// VMA allocator.
		VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;

		// Vulkan swapchain.
		std::unique_ptr<Swapchain> m_swapchain = nullptr;
		
		// Vulkan bindless manager.
		std::unique_ptr<BindlessManager> m_bindlessManager = nullptr;

		// All sampler manager.
		std::unique_ptr<GPUSamplerManager> m_samplerManager = nullptr;

		// Engine builtin textures.
		BuiltinTextures m_builtinTextures;

		// Graphics family command pool with RESET bit flag.
		std::unique_ptr<CommandPoolResetable> m_graphicsCommandPool;

		// Shader compiler of context.
		std::unique_ptr<ShaderCompilerManager> m_shaderCompiler = nullptr;
		std::unique_ptr<ShaderLibrary> m_shaderLibrary = nullptr;
		std::unique_ptr<PipelineLayoutManager> m_pipelineLayoutManager = nullptr;
		std::unique_ptr<PipelineContainer> m_pipelineContainer = nullptr;
		std::unique_ptr<ImGuiManager> m_imguiManager = nullptr;
	};

	// Helper function save some time.
	extern Context& getContext();

	// Helper function save some time.
	static inline const auto* getAllocationCallbacks() 
	{ 
		return getContext().getAllocationCallbacks(); 
	}

	// Helper function save some time.
	static inline VkDevice getDevice()
	{
		return getContext().getDevice();
	}

	// Helper function save some time.
	static inline auto& getBindless()
	{
		return getContext().getBindlessManger();
	}

	// Helper function save some time.
	static inline auto& getSamplers()
	{
		return getContext().getSamplerManager();
	}

	static inline auto getVMA()
	{
		return getContext().getVMA();
	}

	extern void setResourceName(VkObjectType objectType, uint64 handle, const char* name);
}