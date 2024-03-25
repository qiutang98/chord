#define VOLK_IMPLEMENTATION
#include <volk/volk.h>

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <graphics/graphics.h>
#include <utils/cvar.h>
#include <application/application.h>
#include <graphics/helper.h>
#include <graphics/swapchain.h>
#include <graphics/bindless.h>
#include <graphics/resource.h>
#include <graphics/shadercompiler.h>
#include <graphics/shader.h>

namespace chord::graphics
{
	static bool bGraphicsQueueMajorPriority = false;
	static AutoCVarRef<bool> cVarGraphicsQueueMajorPriority(
		"r.graphics.queue.majorPriority",
		bGraphicsQueueMajorPriority,
		"Enable vulkan major queue with higher priority.",
		EConsoleVarFlags::ReadOnly
	);

	static bool bGraphicsDebugMarkerEnable = true;
	static AutoCVarRef<bool> cVarDebugMarkerEnable(
		"r.graphics.debugmarker",
		bGraphicsDebugMarkerEnable,
		"Enable vulkan object debug marker or not.",
		EConsoleVarFlags::ReadOnly
	);

	static uint32 sDesiredShaderCompilerThreadCount = 4;
	static AutoCVarRef<uint32> cVarDesiredShaderCompilerThreadCount(
		"r.graphics.shadercompiler.threadcount",
		sDesiredShaderCompilerThreadCount,
		"Desired shader compiler max used thread count.",
		EConsoleVarFlags::ReadOnly
	);

	static uint32 sFreeShaderCompilerThreadCount = 1;
	static AutoCVarRef<uint32> cVarFreeShaderCompilerThreadCount(
		"r.graphics.shadercompiler.freeThreadCount",
		sFreeShaderCompilerThreadCount,
		"Free shader compiler thread count.",
		EConsoleVarFlags::ReadOnly
	);

	namespace debugUtils
	{
		static int32 sGraphicsDebugUtilsLevel = 1;
		static AutoCVarRef<int32> cVarGraphicsDebugUtilsLevel(
			"r.graphics.debugUtils.level",
			sGraphicsDebugUtilsLevel,
			"Debug utils output info levels, 0 is off, 1 is only error, 2 is warning|error, 3 is info|warning|error, 4 is all message capture from validation.",
			EConsoleVarFlags::ReadOnly
		);

		static inline bool IsVerseEnable()   { return sGraphicsDebugUtilsLevel >= 4; }
		static inline bool IsInfoEnable()    { return sGraphicsDebugUtilsLevel >= 3; }
		static inline bool IsWarningEnable() { return sGraphicsDebugUtilsLevel >= 2; }
		static inline bool IsErrorEnable()   { return sGraphicsDebugUtilsLevel >= 1; }

	#if CHORD_DEBUG
		static bool sGraphicsDebugUtilsExitWhenError = true;
		static AutoCVarRef<bool> cVarGraphicsDebugUtilsExitWhenError(
			"r.graphics.debugUtils.exitWhenError",
			sGraphicsDebugUtilsExitWhenError,
			"Debug utils exit application when meet error or not."
		);
	#endif 

		static auto& getValidationLogger()
		{
			static auto logger = chord::LoggerSystem::get().registerLogger("GraphicsValidation");
			return *logger;
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL messengerCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
			void* userData)
		{
			const bool bVerse   = IsVerseEnable()   && (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT);
			const bool bInfo    = IsInfoEnable()    && (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT);
			const bool bWarning = IsWarningEnable() && (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT);
			const bool bError   = IsErrorEnable()   && (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);

			auto validationCallback = [&]()
			{
				     if (bVerse)   { getValidationLogger().trace(callbackData->pMessage); }
				else if (bInfo)    { getValidationLogger().info(callbackData->pMessage); }
				else if (bWarning) { getValidationLogger().warn(callbackData->pMessage); }
				else if (bError)   { getValidationLogger().error(callbackData->pMessage); }
			};

			if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
			{
				validationCallback();
			}

		#if CHORD_DEBUG
			// Exit application if config require.
			if (bError && sGraphicsDebugUtilsExitWhenError)
			{
				return VK_TRUE;
			}
		#endif

			return VK_FALSE;
		}
		
		static inline auto configMessengerCreateInfo(const Context::InitConfig& config)
		{
			VkDebugUtilsMessengerCreateInfoEXT ci { };

			ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			ci.pNext = nullptr;
			ci.messageType = 0x0;
			ci.messageSeverity = 0x0;
			ci.pfnUserCallback = messengerCallback;

			// Config message severity.
			if (IsVerseEnable())   ci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
			if (IsInfoEnable())    ci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
			if (IsWarningEnable()) ci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
			if (IsErrorEnable())   ci.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

			// Config message type.
			if (config.bValidation) ci.messageType |= VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;

			return ci;
		}
	}

	spdlog::logger& log::get()
	{
		static auto logger = chord::LoggerSystem::get().registerLogger("Graphics");
		return *logger;
	}

	PhysicalDeviceFeatures::PhysicalDeviceFeatures()
	{
		core11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		core12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		core13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

		// KHR.
		accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		raytracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;

		// EXT.
		extendedDynamicState3Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
		extendedDynamicState2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
	}

	void** PhysicalDeviceFeatures::stepNextPtr(void** ppNext)
	{
		graphics::stepNextPtr(ppNext, this->core11Features);
		graphics::stepNextPtr(ppNext, this->core12Features);
		graphics::stepNextPtr(ppNext, this->core13Features);
		graphics::stepNextPtr(ppNext, this->rayQueryFeatures);
		graphics::stepNextPtr(ppNext, this->raytracingPipelineFeatures);
		graphics::stepNextPtr(ppNext, this->accelerationStructureFeatures);
		graphics::stepNextPtr(ppNext, this->extendedDynamicState2Features);
		graphics::stepNextPtr(ppNext, this->extendedDynamicState3Features);

		return ppNext;
	}

	PhysicalDeviceProperties::PhysicalDeviceProperties()
	{
		deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
		descriptorIndexingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;

		// KHR.
		accelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR ;
	}

	void** PhysicalDeviceProperties::getNextPtr()
	{
		auto pNext = graphics::getNextPtr(this->deviceProperties2);

		graphics::stepNextPtr(pNext, this->subgroupProperties);
		graphics::stepNextPtr(pNext, this->descriptorIndexingProperties);
		graphics::stepNextPtr(pNext, this->accelerationStructureProperties);

		return pNext;
	}

	GPUBufferRef Context::createStageUploadBuffer(const std::string& name, SizedBuffer data)
	{
		VkBufferCreateInfo ci{ };
		ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		ci.size  = data.size;
		ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo vmaAllocInfo = {};
		vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		return std::make_shared<GPUBuffer>(name, ci, vmaAllocInfo, data);
	}

	bool Context::init(const InitConfig& inputConfig)
	{
		m_initConfig = inputConfig;

		// Pre-return if application unvalid.
		if (!Application::get().isValid())
		{
			return false;
		}

		// Volk initialize.
		{
			if (volkInitialize() != VK_SUCCESS)
			{
				LOG_GRAPHICS_ERROR("Fail to initialize volk, graphics context init failed!");
				return false;
			}

			auto version = volkGetInstanceVersion();
			LOG_GRAPHICS_TRACE("Volk initialized with vulkan SDK version '{0}.{1}.{2}'.",
				VK_VERSION_MAJOR(version),
				VK_VERSION_MINOR(version),
				VK_VERSION_PATCH(version));
		}

		// Sup struct for enable feature or extension, when bEnable is nullptr, will force enable.
		auto enableIfExist = [](bool* pEnable, const char* name, const char* feature, auto pMemberPtr, const auto& supports, std::vector<const char*>&enableds)
		{
			bool bForce  = (pEnable == nullptr);
			bool bEnable = true;

			if (!bForce)
			{
				bEnable = *pEnable;
				if (!bEnable)
				{
					return;
				}
			}

			bEnable = false;
			for (const auto& support : supports)
			{
				if (same(name, support.*pMemberPtr))
				{
					bEnable = true;
					break;
				}
			}

			if (bEnable)
			{
				enableds.push_back(name);
			}
			else
			{
				LOG_GRAPHICS_ERROR("'{0}' unvalid, the feature {1} will disable.", name, feature);
				checkGraphics(!bForce);

				// Update enable state if require.
				if (pEnable)
				{
					*pEnable = bEnable;
				}
			}
		};

		LOG_GRAPHICS_TRACE("Creating vulkan instance...");
		{
			VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
			appInfo.pApplicationName   = "vulkan";
			appInfo.applicationVersion = 0U;
			appInfo.pEngineName        = "chord";
			appInfo.engineVersion      = 0U;
			appInfo.apiVersion         = VK_API_VERSION_1_3;

			std::vector<const char*> layers{ };
			{
				uint32 instanceLayerCount;
				checkVkResult(vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr));
				std::vector<VkLayerProperties> supportedInstanceLayers(instanceLayerCount);
				checkVkResult(vkEnumerateInstanceLayerProperties(&instanceLayerCount, supportedInstanceLayers.data()));

				enableIfExist(&m_initConfig.bValidation, "VK_LAYER_KHRONOS_validation", "Validation", &VkLayerProperties::layerName, supportedInstanceLayers, layers);
			}

			std::vector<const char*> extensions{ };
			{
				uint32 instanceExtensionCount;
				checkVkResult(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr));
				std::vector<VkExtensionProperties> availableInstanceExtensions(instanceExtensionCount);
				checkVkResult(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, availableInstanceExtensions.data()));

				enableIfExist(&m_initConfig.bDebugUtils, VK_EXT_DEBUG_UTILS_EXTENSION_NAME, "DebugUtils", &VkExtensionProperties::extensionName, availableInstanceExtensions, extensions);

				// GLFW special extension.
				if (m_initConfig.bGLFW)
				{
					uint32 glfwExtensionCount = 0;
					const char** glfwExtensions;
					glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

					for (auto i = 0; i < glfwExtensionCount; i++)
					{
						enableIfExist(&m_initConfig.bGLFW, glfwExtensions[i], "GLFW", &VkExtensionProperties::extensionName, availableInstanceExtensions, extensions);
					}
				}
			}

			for (auto i = 0; i < layers.size(); i++)
			{
				LOG_GRAPHICS_INFO("Instance layer enabled #{0}: '{1}'.", i, layers[i]);
			}

			for (auto i = 0; i < extensions.size(); i++)
			{
				LOG_GRAPHICS_INFO("Instance extension enabled #{0}: '{1}'.", i, extensions[i]);
			}

			// Instance info.
			VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
			instanceInfo.pApplicationInfo = &appInfo;
			instanceInfo.enabledExtensionCount = static_cast<uint32>(extensions.size());
			instanceInfo.ppEnabledExtensionNames = extensions.data();
			instanceInfo.enabledLayerCount = static_cast<uint32>(layers.size());
			instanceInfo.ppEnabledLayerNames = layers.data();

			VkDebugUtilsMessengerCreateInfoEXT debugUtilsCreateInfo{ };

			// Instance create info pNext chain.
			{
				auto pNext = getNextPtr(instanceInfo);

				if (m_initConfig.bDebugUtils)
				{
					debugUtilsCreateInfo = debugUtils::configMessengerCreateInfo(m_initConfig);
					stepNextPtr(pNext, debugUtilsCreateInfo);
				}
			}

			// create vulkan instance.
			if (vkCreateInstance(&instanceInfo, m_initConfig.pAllocationCallbacks, &m_instance) != VK_SUCCESS)
			{
				LOG_GRAPHICS_ERROR("Fail to create vulkan instance.");
				return false;
			}

			// Post create instance processing.
			{
				volkLoadInstance(m_instance);

				if (m_initConfig.bDebugUtils)
				{
					checkVkResult(vkCreateDebugUtilsMessengerEXT(m_instance, &debugUtilsCreateInfo, m_initConfig.pAllocationCallbacks, &m_debugUtilsHandle));
				}
			}
		}

		LOG_GRAPHICS_TRACE("Choosing GPU...");
		{
			uint32 physicalDeviceCount;
			checkVkResult(vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, nullptr));
			std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
			checkVkResult(vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, physicalDevices.data()));

			checkGraphicsMsgf(physicalDeviceCount > 0, "No GPU support vulkan on your computer.");

			bool bExistDiscreteGPU = false;
			for (auto& GPU : physicalDevices)
			{
				VkPhysicalDeviceProperties deviceProperties;
				vkGetPhysicalDeviceProperties(GPU, &deviceProperties);
				if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				{
					bExistDiscreteGPU = true;
					m_physicalDevice = GPU;

					// When find first discrete GPU, just use it.
					break;
				}
			}

			if (!bExistDiscreteGPU)
			{
				LOG_GRAPHICS_WARN("No discrete GPU found, using default GPU as fallback in your computer, may loss some performance.");
				m_physicalDevice = physicalDevices[0];
			}

			// Query and cache GPU properties.
			{
				auto& props = m_physicalDeviceProperties;

				// Query memory.
				vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &props.memoryProperties);

				// 
				void** ppNext = props.getNextPtr();

				vkGetPhysicalDeviceProperties2(m_physicalDevice, &props.deviceProperties2);
				props.deviceProperties = props.deviceProperties2.properties;

				LOG_GRAPHICS_INFO("Application select GPU '{0}'.", props.deviceProperties.deviceName);
			}

			// Query and cache GPU features.
			{
				m_physicalDeviceFeatures = {};
				VkPhysicalDeviceFeatures2 deviceFeatures2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

				void** ppNext = getNextPtr(deviceFeatures2);

				// Add physics device features chain.
				ppNext = m_physicalDeviceFeatures.stepNextPtr(ppNext);

				// Query.
				vkGetPhysicalDeviceFeatures2(m_physicalDevice, &deviceFeatures2);

				// Update core 1.0 features.
				m_physicalDeviceFeatures.core10Features = deviceFeatures2.features;

				// Bindless relative support state check.
				checkGraphics(m_physicalDeviceFeatures.core12Features.shaderSampledImageArrayNonUniformIndexing);
				checkGraphics(m_physicalDeviceFeatures.core12Features.descriptorBindingSampledImageUpdateAfterBind);
				checkGraphics(m_physicalDeviceFeatures.core12Features.shaderUniformBufferArrayNonUniformIndexing);
				checkGraphics(m_physicalDeviceFeatures.core12Features.descriptorBindingUniformBufferUpdateAfterBind);
				checkGraphics(m_physicalDeviceFeatures.core12Features.shaderStorageBufferArrayNonUniformIndexing);
				checkGraphics(m_physicalDeviceFeatures.core12Features.descriptorBindingStorageBufferUpdateAfterBind);
				checkGraphics(m_physicalDeviceFeatures.core12Features.descriptorBindingStorageImageUpdateAfterBind);
			}

			// Query queue infos.
			{
				uint32 graphicsQueueCount { 0 };
				uint32 computeQueueCount  { 0 };
				uint32 copyQueueCount     { 0 };
				uint32 sparseBindingCount { 0 };
				uint32 videoDecodeCount   { 0 };
				uint32 videoEncodeCount   { 0 };

				uint32 queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

				// NOTE: QueueFamilies sort by VkQueueFlagBits in my nVidia graphics card, no ensure the order is same with AMD graphics card.
				std::vector<uint32> queueTypeIndices(queueFamilyCount);
				for (uint32 i = 0; i < queueFamilyCount; i++)
				{
					queueTypeIndices[i] = i;
				}

				// Sort by importance type.
				std::sort(queueTypeIndices.begin(), queueTypeIndices.end(), [&](const auto& A, const auto& B)
				{
					int32 scoreA = 0;
					int32 scoreB = 0;

					auto countScore = [&](auto type, int32 weight)
					{
						scoreA += (queueFamilies[A].queueFlags & type ? 1 : 0) * queueFamilies[A].queueCount * weight;
						scoreB += (queueFamilies[B].queueFlags & type ? 1 : 0) * queueFamilies[B].queueCount * weight;
					};

					countScore(VK_QUEUE_GRAPHICS_BIT, 100000);       // Graphics major queue, most importance.
					countScore(VK_QUEUE_COMPUTE_BIT, 10000);         // Compute queues, importance.
					countScore(VK_QUEUE_TRANSFER_BIT, 5000);         // Copy queues, importance.
					countScore(VK_QUEUE_VIDEO_ENCODE_BIT_KHR, 2500); // Video encode queues, maybe importance.
					countScore(VK_QUEUE_VIDEO_DECODE_BIT_KHR, 1000); // Video decode queues.
					countScore(VK_QUEUE_SPARSE_BINDING_BIT, 500);    // Sparse binding queues, current seem useless.

					return scoreA > scoreB;
				});

				std::unordered_set<uint32> soloUsedFamilies {};
				auto setQueueFamily = [&](auto bit, auto& count, auto& family, const char* type, bool bSolo)
				{
					for (uint32 i = 0; i < queueFamilies.size(); i++)
					{
						auto queueIndex = queueTypeIndices[i];
						const auto& queueFamily = queueFamilies[queueIndex];
						if (!soloUsedFamilies.contains(queueIndex) && (queueFamily.queueFlags & bit))
						{
							count = queueFamily.queueCount;
							family = queueIndex;

							LOG_GRAPHICS_INFO("Found {2} family at #{0} with count: {1}.", queueIndex, queueFamily.queueCount, type);
							break;
						}
					}

					if (bSolo)
					{
						soloUsedFamilies.emplace(family.get());
					}
				};

				// Always can find match queues.
				setQueueFamily(VK_QUEUE_GRAPHICS_BIT, graphicsQueueCount, m_gpuQueuesInfo.graphicsFamily, "graphics", true);
				setQueueFamily(VK_QUEUE_COMPUTE_BIT, computeQueueCount, m_gpuQueuesInfo.computeFamily, "compute", true);
				setQueueFamily(VK_QUEUE_TRANSFER_BIT, copyQueueCount, m_gpuQueuesInfo.copyFamily, "copy", true);

				// Not sure can found or not.
				setQueueFamily(VK_QUEUE_VIDEO_ENCODE_BIT_KHR, videoEncodeCount, m_gpuQueuesInfo.videoEncodeFamily, "video encode", true);
				setQueueFamily(VK_QUEUE_VIDEO_DECODE_BIT_KHR, videoDecodeCount, m_gpuQueuesInfo.videoDecodeFamily, "video decode", true);
				setQueueFamily(VK_QUEUE_SPARSE_BINDING_BIT, sparseBindingCount, m_gpuQueuesInfo.sparseBindingFamily, "sparse binding", true);

				m_gpuQueuesInfo.graphcisQueues.resize(graphicsQueueCount);
				m_gpuQueuesInfo.computeQueues.resize(computeQueueCount);
				m_gpuQueuesInfo.copyQueues.resize(copyQueueCount);
				m_gpuQueuesInfo.spatialBindingQueues.resize(sparseBindingCount);
				m_gpuQueuesInfo.videoDecodeQueues.resize(videoDecodeCount);
				m_gpuQueuesInfo.videoEncodeQueues.resize(videoEncodeCount);
			}
		}

		// Device create.
		LOG_GRAPHICS_TRACE("Creating vulkan logic device...");
		{
			LOG_GRAPHICS_TRACE("Search and try open extensions...");
			std::vector<const char*> deviceExtensionNames{ };
			{
				// Query all useful device extensions.
				uint32 deviceExtensionCount;
				vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &deviceExtensionCount, nullptr);
				std::vector<VkExtensionProperties> available(deviceExtensionCount);
				vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &deviceExtensionCount, available.data());

				// Force enable some device extensions.
				enableIfExist(nullptr, VK_KHR_MAINTENANCE1_EXTENSION_NAME, "Maintenance1", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				enableIfExist(nullptr, VK_KHR_MAINTENANCE2_EXTENSION_NAME, "Maintenance2", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				enableIfExist(nullptr, VK_KHR_MAINTENANCE3_EXTENSION_NAME, "Maintenance3", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				enableIfExist(nullptr, VK_KHR_MAINTENANCE1_EXTENSION_NAME, "Maintenance1", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				enableIfExist(nullptr, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, "PushDescriptor", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				enableIfExist(nullptr, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, "MemoryBudge", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				

				// GLFW
				enableIfExist(&m_initConfig.bGLFW, VK_KHR_SWAPCHAIN_EXTENSION_NAME, "GLFW", &VkExtensionProperties::extensionName, available, deviceExtensionNames);

				// Hdr.
				m_initConfig.bHDR &= m_initConfig.bGLFW;
				enableIfExist(&m_initConfig.bHDR, VK_EXT_HDR_METADATA_EXTENSION_NAME, "HDR", &VkExtensionProperties::extensionName, available, deviceExtensionNames);

				// RTX.
				enableIfExist(&m_initConfig.bRaytracing, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, "Raytracing", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				enableIfExist(&m_initConfig.bRaytracing, VK_KHR_RAY_QUERY_EXTENSION_NAME, "Raytracing", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				enableIfExist(&m_initConfig.bRaytracing, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, "Raytracing", &VkExtensionProperties::extensionName, available, deviceExtensionNames);
				enableIfExist(&m_initConfig.bRaytracing, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, "Raytracing", &VkExtensionProperties::extensionName, available, deviceExtensionNames);

				for (auto i = 0; i < deviceExtensionNames.size(); i++)
				{
					LOG_GRAPHICS_INFO("Enable device extension #{0}: '{1}'.", i, deviceExtensionNames[i]);
				}
			}

			LOG_GRAPHICS_TRACE("Search and try open features...");
			{
				auto& s = m_physicalDeviceFeatures;
				auto& e = m_physicalDeviceFeaturesEnabled;

				#define FORCE_ENABLE(x) checkGraphics(s.x == VK_TRUE); e.x = VK_TRUE; LOG_GRAPHICS_INFO("Enable '{0}'.", #x);
				{
					// Vulkan 1.0 core.
					FORCE_ENABLE(core10Features.samplerAnisotropy);
					FORCE_ENABLE(core10Features.depthClamp);
					FORCE_ENABLE(core10Features.shaderSampledImageArrayDynamicIndexing);
					FORCE_ENABLE(core10Features.multiDrawIndirect);
					FORCE_ENABLE(core10Features.drawIndirectFirstInstance);
					FORCE_ENABLE(core10Features.independentBlend);
					FORCE_ENABLE(core10Features.multiViewport);
					FORCE_ENABLE(core10Features.fragmentStoresAndAtomics);
					FORCE_ENABLE(core10Features.shaderInt16);
					FORCE_ENABLE(core10Features.fillModeNonSolid);
					FORCE_ENABLE(core10Features.depthBiasClamp);

					// Vulkan 1.1 core.
					FORCE_ENABLE(core11Features.shaderDrawParameters);
					FORCE_ENABLE(core11Features.uniformAndStorageBuffer16BitAccess);
					FORCE_ENABLE(core11Features.storageBuffer16BitAccess);

					// Vulkan 1.2 core.
					FORCE_ENABLE(core12Features.drawIndirectCount);
					FORCE_ENABLE(core12Features.imagelessFramebuffer);
					FORCE_ENABLE(core12Features.separateDepthStencilLayouts);
					FORCE_ENABLE(core12Features.descriptorIndexing);
					FORCE_ENABLE(core12Features.runtimeDescriptorArray);
					FORCE_ENABLE(core12Features.descriptorBindingPartiallyBound);
					FORCE_ENABLE(core12Features.descriptorBindingVariableDescriptorCount);
					FORCE_ENABLE(core12Features.shaderSampledImageArrayNonUniformIndexing);
					FORCE_ENABLE(core12Features.descriptorBindingUpdateUnusedWhilePending);
					FORCE_ENABLE(core12Features.descriptorBindingSampledImageUpdateAfterBind);
					FORCE_ENABLE(core12Features.descriptorBindingStorageBufferUpdateAfterBind);
					FORCE_ENABLE(core12Features.shaderStorageBufferArrayNonUniformIndexing);
					FORCE_ENABLE(core12Features.shaderUniformBufferArrayNonUniformIndexing);
					FORCE_ENABLE(core12Features.descriptorBindingUniformBufferUpdateAfterBind);
					FORCE_ENABLE(core12Features.descriptorBindingStorageImageUpdateAfterBind);
					
					FORCE_ENABLE(core12Features.timelineSemaphore);
					FORCE_ENABLE(core12Features.bufferDeviceAddress);
					FORCE_ENABLE(core12Features.shaderFloat16);
					FORCE_ENABLE(core12Features.storagePushConstant8);
					FORCE_ENABLE(core12Features.hostQueryReset);
					FORCE_ENABLE(core12Features.storageBuffer8BitAccess);
					FORCE_ENABLE(core12Features.uniformAndStorageBuffer8BitAccess);

					// Vulkan 1.3 core.
					FORCE_ENABLE(core13Features.dynamicRendering);
					FORCE_ENABLE(core13Features.synchronization2);
					FORCE_ENABLE(core13Features.maintenance4);

					// EXT dynamic state2.
					FORCE_ENABLE(extendedDynamicState2Features.extendedDynamicState2LogicOp);

					// EXT dynamic state3
					FORCE_ENABLE(extendedDynamicState3Features.extendedDynamicState3DepthClampEnable);
					FORCE_ENABLE(extendedDynamicState3Features.extendedDynamicState3PolygonMode);
				}
				#undef FORCE_ENABLE

				#define OPTIONAL_ENABLE(y, x) if (y) { if (s.x != VK_TRUE) { y = false; } else { e.x = VK_TRUE;  LOG_GRAPHICS_INFO("Enable '{0}'.", #x) } }
				{
					// Raytracing.
					OPTIONAL_ENABLE(m_initConfig.bRaytracing, accelerationStructureFeatures.accelerationStructure);
					OPTIONAL_ENABLE(m_initConfig.bRaytracing, raytracingPipelineFeatures.rayTracingPipeline);
					OPTIONAL_ENABLE(m_initConfig.bRaytracing, rayQueryFeatures.rayQuery);

					// Mesh shader.
				}
				#undef OPTIONAL_ENABLE
			}

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

			// Prepare queue priority. all 0.5f.
			constexpr auto kQueueDefaultPriority = 0.5f;
			std::vector<float> graphicsQueuePriority(m_gpuQueuesInfo.graphcisQueues.size(), kQueueDefaultPriority);
			std::vector<float> computeQueuePriority(m_gpuQueuesInfo.computeQueues.size(), kQueueDefaultPriority);
			std::vector<float> copyQueuePriority(m_gpuQueuesInfo.copyQueues.size(), kQueueDefaultPriority);
			std::vector<float> sparseBindingQueuePriority(m_gpuQueuesInfo.spatialBindingQueues.size(), kQueueDefaultPriority);
			std::vector<float> videoDecodeQueuePriority(m_gpuQueuesInfo.videoDecodeQueues.size(), kQueueDefaultPriority);
			std::vector<float> videoEncodeQueuePriority(m_gpuQueuesInfo.videoEncodeQueues.size(), kQueueDefaultPriority);

			// Priority config.
			{
				if (bGraphicsQueueMajorPriority)
				{
					// Major queue use for present and render UI.
					if (graphicsQueuePriority.size() > 0) graphicsQueuePriority[0] = 1.0f;

					// Major compute queue async compute in realtime.
					if (computeQueuePriority.size() > 0) computeQueuePriority[0] = 0.9f;
				}
			}

			// Update queues' priority.
			{
				auto updateQueuesPriority = [](auto& dest, const auto& src)
				{
					for (auto id = 0; id < dest.size(); id++)
					{
						dest[id].priority = src[id];
					}
				};

				updateQueuesPriority(m_gpuQueuesInfo.graphcisQueues, graphicsQueuePriority);
				updateQueuesPriority(m_gpuQueuesInfo.computeQueues, computeQueuePriority);
				updateQueuesPriority(m_gpuQueuesInfo.copyQueues, copyQueuePriority);

				updateQueuesPriority(m_gpuQueuesInfo.spatialBindingQueues, sparseBindingQueuePriority);
				updateQueuesPriority(m_gpuQueuesInfo.videoDecodeQueues, videoDecodeQueuePriority);
				updateQueuesPriority(m_gpuQueuesInfo.videoEncodeQueues, videoEncodeQueuePriority);
			}

			// Build queue create infos.
			{
				VkDeviceQueueCreateInfo queueCreateInfo { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };

				auto buildQueueCreateInfo = [&](const auto& queues, const auto& family, const auto& priorities)
				{
					if (queues.size() > 0)
					{
						queueCreateInfo.queueFamilyIndex = family.get();
						queueCreateInfo.queueCount = (uint32)queues.size();
						queueCreateInfo.pQueuePriorities = priorities.data();
						queueCreateInfos.push_back(queueCreateInfo);
					}
				};

				buildQueueCreateInfo(m_gpuQueuesInfo.graphcisQueues, m_gpuQueuesInfo.graphicsFamily, graphicsQueuePriority);
				buildQueueCreateInfo(m_gpuQueuesInfo.computeQueues, m_gpuQueuesInfo.computeFamily, computeQueuePriority);
				buildQueueCreateInfo(m_gpuQueuesInfo.copyQueues, m_gpuQueuesInfo.copyFamily, copyQueuePriority);
				buildQueueCreateInfo(m_gpuQueuesInfo.spatialBindingQueues, m_gpuQueuesInfo.sparseBindingFamily, sparseBindingQueuePriority);
				buildQueueCreateInfo(m_gpuQueuesInfo.videoDecodeQueues, m_gpuQueuesInfo.videoDecodeFamily, videoDecodeQueuePriority);
				buildQueueCreateInfo(m_gpuQueuesInfo.videoEncodeQueues, m_gpuQueuesInfo.videoEncodeFamily, videoEncodeQueuePriority);
			}

			// Device create feature.
			VkPhysicalDeviceFeatures2 physicalDeviceFeatures2
			{
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
				.features = m_physicalDeviceFeaturesEnabled.core10Features
			};

			// Final consume.
			VkDeviceCreateInfo createInfo
			{
				.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				.pNext = &physicalDeviceFeatures2,
			};
			// NOTE: If exist other chain, continue use this pNext.
			void** pNextDeviceCreateInfo = getNextPtr(physicalDeviceFeatures2);
			pNextDeviceCreateInfo = m_physicalDeviceFeaturesEnabled.stepNextPtr(pNextDeviceCreateInfo);

			// Queues info.
			createInfo.pQueueCreateInfos = queueCreateInfos.data();
			createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

			// Extensions.
			createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size());
			createInfo.ppEnabledExtensionNames = deviceExtensionNames.data();


			// Current special device layer.
			createInfo.ppEnabledLayerNames = NULL;
			createInfo.enabledLayerCount = 0;

			// Now create device.
			checkVkResult(vkCreateDevice(m_physicalDevice, &createInfo, m_initConfig.pAllocationCallbacks, &m_device));
		}

		// Post device create process.
		{
			volkLoadDevice(m_device);

			// Queue udpate.
			{
				// Get queue id and create a command pool.
				auto updateQueue = [&](auto& queues, const auto& family)
				{
					for (auto id = 0; id < queues.size(); id++)
					{
						vkGetDeviceQueue(m_device, family.get(), id, &queues[id].queue);
					}
				};
				updateQueue(m_gpuQueuesInfo.graphcisQueues, m_gpuQueuesInfo.graphicsFamily);
				updateQueue(m_gpuQueuesInfo.computeQueues, m_gpuQueuesInfo.computeFamily);
				updateQueue(m_gpuQueuesInfo.copyQueues, m_gpuQueuesInfo.copyFamily);
				updateQueue(m_gpuQueuesInfo.spatialBindingQueues, m_gpuQueuesInfo.sparseBindingFamily);
				updateQueue(m_gpuQueuesInfo.videoDecodeQueues, m_gpuQueuesInfo.videoDecodeFamily);
				updateQueue(m_gpuQueuesInfo.videoEncodeQueues, m_gpuQueuesInfo.videoEncodeFamily);
			}

			// Create graphics command pool.
			m_graphicsCommandPool = std::make_unique<CommandPoolResetable>("GraphicsBuiltinCommandPool");

			// Create pipeline cache
			{
				VkPipelineCacheCreateInfo ci {};
				ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
				
				checkVkResult(vkCreatePipelineCache(m_device, &ci, m_initConfig.pAllocationCallbacks, &m_pipelineCache));
			}

			// VMA
			{
				VmaVulkanFunctions vulkanFunctions = {};
				vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
				vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

				VmaAllocatorCreateInfo allocatorInfo = {};
				allocatorInfo.physicalDevice   = m_physicalDevice;
				allocatorInfo.device           = m_device;
				allocatorInfo.instance         = m_instance;
				allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
				allocatorInfo.pVulkanFunctions = &vulkanFunctions;
				allocatorInfo.flags = 
					VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT | 
					VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

				checkVkResult(vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator));
			}

			// Create bindless manager.
			{
				m_bindlessManager = std::make_unique<BindlessManager>();
			}

			// Shader compiler.
			{
				m_shaderCompiler = std::make_unique<ShaderCompilerManager>(sFreeShaderCompilerThreadCount, sDesiredShaderCompilerThreadCount);
				m_shaderLibrary = std::make_unique<ShaderLibrary>();
			}

			{
				m_samplerManager = std::make_unique<GPUSamplerManager>();
			}

			initBuiltinTextures();

			// Create swapchain.
			{
				m_swapchain = std::make_unique<Swapchain>();
			}
		}

		return true;
	}

	bool Context::tick(const ApplicationTickData& tickData)
	{
		const bool bMainMinimized = Application::get().isFramebufferZeroSize();

		auto [contextCmd, contextWaitSemaphore] = m_swapchain->beginFrameCmd(tickData.tickCount);
		{
			// Record.
		}
		helper::endCommandBuffer(contextCmd);

		if (bMainMinimized)
		{
			// Submit to queue.
		}
		else
		{
			// Acquire next present image.
			uint32 backBufferIndex = m_swapchain->acquireNextPresentImage();

			// Submit and present fence.
			{
				VkPipelineStageFlags kWaitFlags = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;

				VkSemaphore waitSemaphores[] =
				{ 
					m_swapchain->getCurrentFrameWaitSemaphore(), 
					contextWaitSemaphore 
				};

				VkSemaphore signalSemaphores[] =
				{ 
					m_swapchain->getCurrentFrameFinishSemaphore() 
				};

				helper::SubmitInfo contextCmdSubmitInfo{ };
				contextCmdSubmitInfo.setWaitStage(&kWaitFlags)
					.setWaitSemaphore(waitSemaphores, countof(waitSemaphores))
					.setSignalSemaphore(signalSemaphores, countof(waitSemaphores))
					.setCommandBuffer(&contextCmd, 1);

				std::vector<VkSubmitInfo> infosRawSubmit{ contextCmdSubmitInfo };
				m_swapchain->submit((uint32)infosRawSubmit.size(), infosRawSubmit.data());
			}
		}

		if (!bMainMinimized)
		{
			m_swapchain->present();
		}

		return true;
	}

	OptionalUint32 Context::findMemoryType(uint32 typeFilter, VkMemoryPropertyFlags properties) const
	{
		for (uint32 i = 0; i < m_physicalDeviceProperties.memoryProperties.memoryTypeCount; i++)
		{
			if ((typeFilter & (1 << i)) &&
				(m_physicalDeviceProperties.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}

		return { };
	}

	void Context::beforeRelease()
	{
		vkDeviceWaitIdle(m_device);
	}

	void Context::release()
	{
		// Swapchain.
		m_swapchain.reset();

		// Clear all builtin textures.
		m_builtinTextures = {};

		// Clear shader compiler.
		m_shaderLibrary.reset();
		m_shaderCompiler.reset();

		// Release bindless manager resources.
		m_bindlessManager.reset();

		// Clear VMA.
		if (m_vmaAllocator != VK_NULL_HANDLE)
		{
			vmaDestroyAllocator(m_vmaAllocator);
			m_vmaAllocator = VK_NULL_HANDLE;
		}

		// Pipeline cache destroy.
		helper::destroyPipelineCache(m_pipelineCache);

		// Clear command pool.
		m_graphicsCommandPool.reset();

		if (m_device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_device, m_initConfig.pAllocationCallbacks);
			m_device = VK_NULL_HANDLE;
		}

		helper::destroyDebugUtilsMessenger(m_debugUtilsHandle);

		if (m_instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_instance, m_initConfig.pAllocationCallbacks);
			m_instance = VK_NULL_HANDLE;
		}
	}

	void Context::initBuiltinTextures()
	{
		auto imageCI1x1  = helper::buildBasicUploadImageCreateInfo(1, 1);
		auto uploadVMACI = helper::buildVMAUploadImageAllocationCI();

		// Sync upload builtin textures.
		{
			m_builtinTextures.white = std::make_shared<GPUTexture>("Builtin::White", imageCI1x1, uploadVMACI);

			SizedBuffer buffer1x1(sizeof(RGBA), (void*)RGBA::kWhite.getData());
			syncUploadTexture(*m_builtinTextures.white, buffer1x1);
		}
		{
			m_builtinTextures.transparent = std::make_shared<GPUTexture>("Builtin::Transparent", imageCI1x1, uploadVMACI);

			SizedBuffer buffer1x1(sizeof(RGBA), (void*)RGBA::kTransparent.getData());
			syncUploadTexture(*m_builtinTextures.transparent, buffer1x1);
		}
		{
			m_builtinTextures.black = std::make_shared<GPUTexture>("Builtin::Black", imageCI1x1, uploadVMACI);

			SizedBuffer buffer1x1(sizeof(RGBA), (void*)RGBA::kBlack.getData());
			syncUploadTexture(*m_builtinTextures.black, buffer1x1);
		}
	}

	void Context::syncUploadTexture(GPUTexture& texture, SizedBuffer data)
	{
		auto stageBuffer = createStageUploadBuffer(texture.getName() + "-syncStageBuffer", data);

		executeImmediatelyMajorGraphics([&](VkCommandBuffer cmd, uint32 family, VkQueue queue)
		{
			const auto range = helper::buildBasicImageSubresource();

			GPUTextureSyncBarrierMasks copyState{};
			copyState.barrierMasks.queueFamilyIndex = family;
			copyState.barrierMasks.accesMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			copyState.imageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			texture.transition(cmd, copyState, range);

			VkBufferImageCopy region{};
			region.bufferOffset      = 0;
			region.bufferRowLength   = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel   = 0;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = { 0, 0, 0 };
			region.imageExtent = texture.getExtent();
			vkCmdCopyBufferToImage(cmd, *stageBuffer, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

			GPUTextureSyncBarrierMasks finalState{};
			finalState.barrierMasks.queueFamilyIndex = family;
			finalState.barrierMasks.accesMask = VK_ACCESS_SHADER_READ_BIT;
			finalState.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			texture.transition(cmd, finalState, range);
		});
	}

	void Context::executeImmediately(VkCommandPool commandPool, uint32 family, VkQueue queue, std::function<void(VkCommandBuffer cb, uint32 family, VkQueue queue)>&& func) const
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = commandPool;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		checkVkResult(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer));

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		checkVkResult(vkBeginCommandBuffer(commandBuffer, &beginInfo));
		func(commandBuffer, family, queue);
		checkVkResult(vkEndCommandBuffer(commandBuffer));

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		checkVkResult(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		checkVkResult(vkQueueWaitIdle(queue));

		vkFreeCommandBuffers(m_device, commandPool, 1, &commandBuffer);
	}

	void Context::executeImmediatelyMajorGraphics(std::function<void(VkCommandBuffer cb, uint32 family, VkQueue queue)>&& func) const
	{
		executeImmediately(getGraphicsCommandPool().pool(), getGraphicsCommandPool().family(), getMajorGraphicsQueue(), std::move(func));
	}

	Context& graphics::getContext()
	{
		return Application::get().getContext();
	}

	void setResourceName(VkObjectType objectType, uint64 handle, const char* name)
	{
		if (!bGraphicsDebugMarkerEnable)
		{
			return;
		}

		static std::mutex kMutexForSetResource;
		{
			std::lock_guard lock(kMutexForSetResource);

			VkDebugUtilsObjectNameInfoEXT nameInfo = {};
			nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
			nameInfo.objectType = objectType;
			nameInfo.objectHandle = handle;
			nameInfo.pObjectName = name;

			vkSetDebugUtilsObjectNameEXT(getDevice(), &nameInfo);
		}
	}

}