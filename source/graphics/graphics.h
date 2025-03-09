#pragma once

#include <graphics/common.h>
#include <graphics/resource.h>
#include <graphics/swapchain.h>
#include <graphics/bindless.h>
#include <graphics/pipeline.h>
#include <graphics/texture_pool.h>
#include <graphics/uploader.h>
#include <graphics/descriptor.h>
#include <graphics/blue_noise.h>

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

		VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures { };
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

	class BuiltinMesh
	{
	public:
		struct BuiltinVertex
		{
			float3 position;
			float3 normal;
			float2 uv;
		};

		// Used for sort.
		uint32 meshTypeUniqueId;
		uint32 indicesCount;

		PoolBufferHostVisible indices  = nullptr;
		PoolBufferHostVisible vertices = nullptr; // position, normal, uv
	};
	using BuiltinMeshRef = std::shared_ptr<BuiltinMesh>;

	class BuiltinResources
	{
	public:
		// Textures
		GPUTextureAssetRef white = nullptr;       // 1x1 RGBA(255,255,255,255)
		GPUTextureAssetRef transparent = nullptr; // 1x1 RGBA(  0,   0,   0,   0)
		GPUTextureAssetRef black = nullptr;       // 1x1 RGBA(  0,   0,   0, 255) 
		GPUTextureAssetRef normal = nullptr;      // 1x1 RGBA(128, 128, 255, 255) 

		// Meshes
		BuiltinMeshRef lowSphere = nullptr;
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

		const auto& getPhysicalDeviceDescriptorIndexingProperties() const { return m_physicalDeviceProperties.descriptorIndexingProperties;  }
		const auto& getPhysicalDeviceProperties() const { return m_physicalDeviceProperties; }
		VkInstance getInstance() const { return m_instance; }
		VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
		VkDevice getDevice() const { return m_device; }
		VkPipelineCache getPipelineCache() const { return m_pipelineCache; }

		// Find memory type from memory.
		OptionalUint32 findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties) const;

		// Major graphics queue is most importance queue with highest priority.
		VkQueue getMajorGraphicsQueue() const { return m_gpuQueuesInfo.graphcisQueues[0].queue; }
		VkQueue getMajorComputeQueue() const { return m_gpuQueuesInfo.computeQueues[0].queue; }

		// Get graphics queues info.
		const auto& getQueuesInfo() const { return m_gpuQueuesInfo; }

		// Vulkan allocation callbacks, register in init config.
		const auto* getAllocationCallbacks() const { return m_initConfig.pAllocationCallbacks; }

		// Current graphics support HDR.
		bool isRequiredHDR() const { return m_initConfig.bHDR; }
		bool isRaytraceSupport() const { return m_initConfig.bRaytracing; }

		// Current application own bindless manager.
		auto& getBindlessManger() { return *m_bindlessManager; }
		const auto& getBindlessManger() const { return *m_bindlessManager; }

		// All samplers managed by SamplerManager, only one instance.
		auto& getSamplerManager() { return *m_samplerManager; }
		const auto& getSamplerManager() const { return *m_samplerManager; }

		// Current application own shader compiler.
		auto& getShaderCompiler() { return *m_shaderCompiler; }
		const auto& getShaderCompiler() const { return *m_shaderCompiler; }

		auto& getShaderLibrary() { return *m_shaderLibrary; }
		const auto& getShaderLibrary() const { return *m_shaderLibrary; }

		auto& getAsyncUploader() { return *m_asyncUploader; }
		const auto& getAsyncUploader() const { return *m_asyncUploader; }

		// Vulkan memory allocator handle.
		VmaAllocator getVMA() const { return m_vmaAllocator; }

		// Engine built textures.
		const BuiltinResources& getBuiltinResources() const { return m_builtinResources; }
		uint32 getWhiteTextureSRV() const;

		// Create a stage upload buffer, commonly only used for engine init.
		HostVisibleGPUBufferRef createStageUploadBuffer(const std::string& name, SizedBuffer data);

		// Sync upload texture, only used for engine init, should not call in actually render pipeline.
		void syncUploadTexture(GPUTexture& texture, SizedBuffer data);

		// Sync execute command buffer, only used for engine init, should not call in actually render pipeline.
		void executeImmediately(VkCommandPool commandPool, uint32 family, VkQueue queue, std::function<void(VkCommandBuffer cb, uint32 family, VkQueue queue)>&& func) const;

		// Sync execute command buffer in main graphics queue, only used for engine init, should not call in actually render pipeline.
		void executeImmediatelyMajorGraphics(std::function<void(VkCommandBuffer cb, uint32 family, VkQueue queue)>&& func) const;
		void executeImmediatelyMajorCompute(std::function<void(VkCommandBuffer cb, uint32 family, VkQueue queue)>&& func) const;

		// Builtin graphics command pool with RESET bit.
		const CommandPoolResetable& getGraphicsCommandPool() const { return *m_graphicsCommandPool; }

		const CommandPoolResetable& getComputeCommandPool() const { return *m_computeCommandPool; }

		const auto& getPipelineLayoutManager() const { return *m_pipelineLayoutManager; }
		auto& getPipelineLayoutManager() { return *m_pipelineLayoutManager; }

		const auto& getPipelineContainer() const { return *m_pipelineContainer; }
		auto& getPipelineContainer() { return *m_pipelineContainer; }

		const auto& getTexturePool() const { return *m_texturePool; }
		auto& getTexturePool() { return *m_texturePool; }

		const auto& getBufferPool() const { return *m_bufferPool; }
		auto& getBufferPool() { return *m_bufferPool; }

		template<class VertexShader, class PixelShader>
		GraphicsPipelineRef graphicsPipe(
			const std::string& name,
			std::vector<VkFormat>&& attachments,
			VkFormat inDepthFormat = VK_FORMAT_UNDEFINED,
			VkFormat inStencilFormat = VK_FORMAT_UNDEFINED,
			VkPrimitiveTopology inTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		inline GraphicsPipelineRef graphicsPipe(
			std::shared_ptr<ShaderModule> vertexShader,
			std::shared_ptr<ShaderModule> pixelShader,
			const std::string& name,
			std::vector<VkFormat>&& attachments,
			VkFormat inDepthFormat = VK_FORMAT_UNDEFINED,
			VkFormat inStencilFormat = VK_FORMAT_UNDEFINED,
			VkPrimitiveTopology inTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		template<class AmplifyShader, class MeshShader, class PixelShader>
		GraphicsPipelineRef graphicsAmplifyMeshPipe(
			const std::string& name,
			std::vector<VkFormat>&& attachments,
			VkFormat inDepthFormat = VK_FORMAT_UNDEFINED,
			VkFormat inStencilFormat = VK_FORMAT_UNDEFINED,
			VkPrimitiveTopology inTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		template<class MeshShader, class PixelShader>
		GraphicsPipelineRef graphicsMeshPipe(
			const std::string& name,
			std::vector<VkFormat>&& attachments,
			VkFormat inDepthFormat = VK_FORMAT_UNDEFINED,
			VkFormat inStencilFormat = VK_FORMAT_UNDEFINED,
			VkPrimitiveTopology inTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		inline GraphicsPipelineRef graphicsMeshShadingPipe(
			std::shared_ptr<ShaderModule> amplifyShader,
			std::shared_ptr<ShaderModule> meshShader,
			std::shared_ptr<ShaderModule> pixelShader,
			const std::string& name,
			std::vector<VkFormat>&& attachments,
			VkFormat inDepthFormat = VK_FORMAT_UNDEFINED,
			VkFormat inStencilFormat = VK_FORMAT_UNDEFINED,
			VkPrimitiveTopology inTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

		inline ComputePipelineRef computePipe(
			std::shared_ptr<ShaderModule> computeShader,
			const std::string& name,
			const std::vector<VkDescriptorSetLayout>& additionalSetLayouts = {}
		);

		template<class ComputeShader>
		ComputePipelineRef computePipe(const std::string& name);

		void waitDeviceIdle() const;
		GPUBufferRef getDummySSBO() const { return m_dummySSBO; }
		GPUBufferRef getDummyUniform() const { return m_dummyUniform; }

		DescriptorFactory descriptorFactoryBegin();
		DescriptorLayoutCache& getDescriptorLayoutCache() { return m_descriptorLayoutCache; }
		const DescriptorLayoutCache& getDescriptorLayoutCache() const { return m_descriptorLayoutCache; }

		void pushDescriptorSet(
			VkCommandBuffer commandBuffer,
			VkPipelineBindPoint pipelineBindPoint,
			VkPipelineLayout layout,
			uint32 set,
			uint32 descriptorWriteCount,
			const VkWriteDescriptorSet* pDescriptorWrites);

		void setPerfMarkerBegin(VkCommandBuffer cmdBuf, const char* name, const math::vec4& color) const;
		void setPerfMarkerEnd(VkCommandBuffer cmdBuf) const;

		BlueNoiseContext& getBlueNoise() { return *m_blueNoise; }
		const BlueNoiseContext& getBlueNoise() const { return *m_blueNoise; }

	public:
		bool init(const InitConfig& config);
		bool tick(const ApplicationTickData& tickData);

		void beforeRelease();
		void release();

		// After context basic init will call and clean this event.
		Events<Context> onInit;

		// Tick event.
		Events<Context, const ApplicationTickData&> onTick;

	private:
		// Sync init builtin resources.
		void initBuiltinResources();

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

		std::mutex m_graphicsLock;

		// VMA allocator.
		VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
		
		// Vulkan bindless manager.
		std::unique_ptr<BindlessManager> m_bindlessManager = nullptr;

		// All sampler manager.
		std::unique_ptr<GPUSamplerManager> m_samplerManager = nullptr;

		// Engine builtin textures.
		BuiltinResources m_builtinResources;
		GPUBufferRef m_dummySSBO;
		GPUBufferRef m_dummyUniform;

		// Blue noise.
		std::unique_ptr<BlueNoiseContext> m_blueNoise = nullptr;

		// Descriptor allocator and layout cache.
		DescriptorAllocator m_descriptorAllocator;
		DescriptorLayoutCache m_descriptorLayoutCache;

		// Graphics family command pool with RESET bit flag.
		std::unique_ptr<CommandPoolResetable> m_graphicsCommandPool;

		//
		std::unique_ptr<CommandPoolResetable> m_computeCommandPool;

		// Shader compiler of context.
		std::unique_ptr<ShaderCompilerManager> m_shaderCompiler = nullptr;
		std::unique_ptr<ShaderLibrary> m_shaderLibrary = nullptr;
		std::unique_ptr<PipelineLayoutManager> m_pipelineLayoutManager = nullptr;
		std::unique_ptr<PipelineContainer> m_pipelineContainer = nullptr;
		std::unique_ptr<ImGuiManager> m_imguiManager = nullptr;
		std::unique_ptr<GPUTexturePool> m_texturePool = nullptr;
		std::unique_ptr<GPUBufferPool> m_bufferPool = nullptr;
		std::unique_ptr<AsyncUploaderManager> m_asyncUploader = nullptr;
	};

	// Helper function save some time.
	extern Context& getContext();

	extern std::string getRuntimeUniqueGPUAssetName(const std::string& in);

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

	struct ScopePerframeMarker
	{
		std::string    name;
		VkCommandBuffer cmd;

		ScopePerframeMarker(GraphicsOrComputeQueue& queue, const std::string& name, const glm::vec4& color = { 1.0f, 1.0f, 1.0f, 1.0f })
			: cmd(queue.getActiveCmd()->commandBuffer), name(name)
		{
			getContext().setPerfMarkerBegin(cmd, name.c_str(), color);
		}

		~ScopePerframeMarker()
		{
			getContext().setPerfMarkerEnd(cmd);
		}
	};
}