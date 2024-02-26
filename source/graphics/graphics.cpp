#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>

#include <graphics/graphics.h>
#include <utils/cvar.h>
#include <application/application.h>

namespace chord::graphics
{
	static bool bGraphicsQueueMajorPriority = 0;
	static AutoCVarRef<bool> cVarGraphicsQueueMajorPriority(
		"r.graphics.queue.majorPriority",
		bGraphicsQueueMajorPriority,
		"Enable vulkan major queue with higher priority.",
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

	bool Context::onTick(const SubsystemTickData& tickData)
	{
		return true;
	}

	bool Context::onInit()
	{
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
			LOG_GRAPHICS_TRACE("Volk initialize, vulkan SDK version {0}.{1}.{2} initialize.",
				VK_VERSION_MAJOR(version),
				VK_VERSION_MINOR(version),
				VK_VERSION_PATCH(version));
		}

		LOG_GRAPHICS_TRACE("Creating vulkan instance...");
		{
			VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
			appInfo.pApplicationName   = Application::get().getName().c_str();
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

				auto layerCheck = [&](const char* name, const char* feature) -> bool
				{
					for (const auto& properties : supportedInstanceLayers)
					{
						if (same(name, properties.layerName))
						{
							return true;
						}
					}

					LOG_GRAPHICS_ERROR(
						"Required instance layer {0} unvalid, the instance layer feature {1} will disable.", name, feature);

					return false;
				};

				auto optionalEnable = [&](bool& bState, const char* name, const char* feature)
				{
					if (bState)
					{
						bState = layerCheck(name, feature);
						if (bState)
						{
							layers.push_back(name);
						}
					}
				};

				// Validation layer.
				optionalEnable(m_initConfig.bValidation, "VK_LAYER_KHRONOS_validation", "Validation");
			}

			std::vector<const char*> extensions{ };
			{
				uint32 instanceExtensionCount;
				checkVkResult(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr));
				std::vector<VkExtensionProperties> availableInstanceExtensions(instanceExtensionCount);
				checkVkResult(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, availableInstanceExtensions.data()));

				auto extensionsCheck = [&](const char* name, const char* feature) -> bool
				{
					for (const auto& properties : availableInstanceExtensions)
					{
						if (same(name, properties.extensionName))
						{
							return true;
						}
					}

					LOG_GRAPHICS_ERROR(
						"Required instance extension {0} unvalid, the instance extension feature {1} will disable.", name, feature);
					return false;
				};

				auto optionalEnable = [&](bool& bState, const char* name, const char* feature)
				{
					if (bState)
					{
						bState = extensionsCheck(name, feature);
						if (bState)
						{
							extensions.push_back(name);
						}
					}
				};

				optionalEnable(m_initConfig.bDebugUtils, VK_EXT_DEBUG_UTILS_EXTENSION_NAME, "DebugUtils");

				// GLFW special extension.
				if (m_initConfig.bGLFW)
				{
					uint32 glfwExtensionCount = 0;
					const char** glfwExtensions;
					glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

					for (auto i = 0; i < glfwExtensionCount; i++)
					{
						optionalEnable(m_initConfig.bGLFW, glfwExtensions[i], "GLFW");
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

			// Instance create info pNext.
			{
				const void** pNextInstanceCreate = &instanceInfo.pNext;
				if (m_initConfig.bDebugUtils)
				{
					debugUtilsCreateInfo = debugUtils::configMessengerCreateInfo(m_initConfig);

					(*pNextInstanceCreate) = &debugUtilsCreateInfo;
					pNextInstanceCreate = &debugUtilsCreateInfo.pNext;
				}
			}

			// create vulkan instance.
			if (vkCreateInstance(&instanceInfo, nullptr, &m_instance) != VK_SUCCESS)
			{
				LOG_GRAPHICS_ERROR("Fail to create vulkan instance.");
				return false;
			}

			// Post create instance processing.
			{
				volkLoadInstance(m_instance);

				if (m_initConfig.bDebugUtils)
				{
					checkVkResult(vkCreateDebugUtilsMessengerEXT(m_instance, &debugUtilsCreateInfo, nullptr, &m_debugUtilsHandle));
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

				vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &props.memoryProperties);
				vkGetPhysicalDeviceProperties(m_physicalDevice, &props.deviceProperties);
				LOG_GRAPHICS_INFO("Application select GPU '{0}'.", props.deviceProperties.deviceName);

				props.deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
				props.deviceProperties2.pNext = &props.descriptorIndexingProperties;

				props.descriptorIndexingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
				props.descriptorIndexingProperties.pNext = &props.accelerationStructureProperties;

				props.accelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
				props.accelerationStructureProperties.pNext = &props.subgroupProperties;

				props.subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
				props.subgroupProperties.pNext = nullptr;

				vkGetPhysicalDeviceProperties2(m_physicalDevice, &props.deviceProperties2);
			}

			// Query and cache GPU features.
			{
				m_physicalDeviceFeatures = {};
				VkPhysicalDeviceFeatures2 deviceFeatures2 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

				m_physicalDeviceFeatures.stepNextPtr(deviceFeatures2);

				vkGetPhysicalDeviceFeatures2(m_physicalDevice, &deviceFeatures2);

				m_physicalDeviceFeatures.core10Features = deviceFeatures2.features;
			}

			// Query queue infos.
			{
				uint32 graphicsQueueCount { 0 };
				uint32 computeQueueCount  { 0 };
				uint32 copyQueueCount     { 0 };
				uint32 sparseBindingCount { 0 };
				uint32 videoDecodeCount   { 0 };

				uint32 queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

				// NOTE: QueueFamilies sort by VkQueueFlagBits in my nVidia graphics card, no ensure the order is same with AMD graphics card.
				for (uint32 queueIndex = 0; queueIndex < queueFamilies.size(); queueIndex ++)
				{
					auto& queueFamily = queueFamilies[queueIndex];
					auto& info = m_gpuQueuesInfo;

					const bool bSupportGraphics      = queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT;
					const bool bSupportCompute       = queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT;
					const bool bSupportCopy          = queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT; 
					const bool bSupportSparseBinding = queueFamily.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT;
					const bool bSupportVideoDecode   = queueFamily.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR;

					if (bSupportGraphics && !info.graphicsFamily.isValid())
					{
						graphicsQueueCount = queueFamily.queueCount;
						info.graphicsFamily = queueIndex;

						LOG_GRAPHICS_INFO("Found graphics family at #{0} with count: {1}.", queueIndex, queueFamily.queueCount);
					}
					else if (bSupportCompute && !info.computeFamily.isValid())
					{
						computeQueueCount = queueFamily.queueCount;
						info.computeFamily = queueIndex;

						LOG_GRAPHICS_INFO("Found compute family at #{0} with count: {1}.", queueIndex, queueFamily.queueCount);
					}
					else if (bSupportCopy && !info.copyFamily.isValid())
					{
						copyQueueCount = queueFamily.queueCount;
						info.copyFamily = queueIndex;

						LOG_GRAPHICS_INFO("Found copy family at #{0} with count: {1}.", queueIndex, queueFamily.queueCount);
					}
					else if (bSupportSparseBinding && !info.sparseBindingFamily.isValid())
					{
						sparseBindingCount = queueFamily.queueCount;
						info.sparseBindingFamily = queueIndex;

						LOG_GRAPHICS_INFO("Found sparse binding family at #{0} with count: {1}.", queueIndex, queueFamily.queueCount);
					}
					else if (bSupportVideoDecode && !info.videoDecodeFamily.isValid())
					{
						videoDecodeCount = queueFamily.queueCount;
						info.videoDecodeFamily = queueIndex;

						LOG_GRAPHICS_INFO("Found video decode family at #{0} with count: {1}.", queueIndex, queueFamily.queueCount);
					}
				}

				m_gpuQueuesInfo.graphcisQueues.resize(graphicsQueueCount);
				m_gpuQueuesInfo.computeQueues.resize(computeQueueCount);
				m_gpuQueuesInfo.copyQueues.resize(copyQueueCount);
				m_gpuQueuesInfo.spatialBindingQueues.resize(sparseBindingCount);
				m_gpuQueuesInfo.videoDecodeQueues.resize(videoDecodeCount);
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
				std::vector<VkExtensionProperties> availableDeviceExtensions(deviceExtensionCount);
				vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &deviceExtensionCount, availableDeviceExtensions.data());

				auto existDeviceExtension = [&](const char* name, const char* feature)
				{
					for (auto& availableExtension : availableDeviceExtensions)
					{
						if (same(availableExtension.extensionName, name))
						{
							return true;
						}
					}

					return false;
				};
				// Force enable some device extensions.
				{
					auto forceEnable = [&](const char* name, const char* feature)
					{
						if (!existDeviceExtension(name, feature))
						{
							LOG_GRAPHICS_FATAL("Force enable device extension {0} unvalid, the necessary feature {1} will disabled.", name, feature);
						}
						deviceExtensionNames.push_back(name);
					};

					// Maintenance extension.
					forceEnable(VK_KHR_MAINTENANCE1_EXTENSION_NAME, "Maintenance1");
					forceEnable(VK_KHR_MAINTENANCE2_EXTENSION_NAME, "Maintenance2");
					forceEnable(VK_KHR_MAINTENANCE3_EXTENSION_NAME, "Maintenance3");

					// Push descriptor.
					forceEnable(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, "PushDescriptor");
				}

				// Optional device extensions.
				{
					auto optionalEnable = [&](bool& bState, const char* name, const char* feature)
					{
						if (bState)
						{
							if (existDeviceExtension(name, feature))
							{
								deviceExtensionNames.push_back(name);
							}
							else
							{
								LOG_GRAPHICS_ERROR("Required device extension {0} unvalid, the feature {1} will disabled.", name, feature);
								bState = false;
							}
						}
					};

					// GLFW.
					optionalEnable(m_initConfig.bGLFW, VK_KHR_SWAPCHAIN_EXTENSION_NAME, "GLFW");

					// HDR.
					if (m_initConfig.bGLFW)
					{
						optionalEnable(m_initConfig.bHDR, VK_EXT_HDR_METADATA_EXTENSION_NAME, "HDR");
					}

					// Raytracing.
					optionalEnable(m_initConfig.bRaytracing, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,   "Raytracing");
					optionalEnable(m_initConfig.bRaytracing, VK_KHR_RAY_QUERY_EXTENSION_NAME,                "Raytracing");
					optionalEnable(m_initConfig.bRaytracing, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,     "Raytracing");
					optionalEnable(m_initConfig.bRaytracing, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, "Raytracing");
				}

				for (auto i = 0; i < deviceExtensionNames.size(); i++)
				{
					LOG_GRAPHICS_INFO("Enable device extension #{0}: '{1}'.", i, deviceExtensionNames[i]);
				}
			}

			LOG_GRAPHICS_TRACE("Search and try open features...");
			{
				auto& s = m_physicalDeviceFeatures;
				auto& e = m_physicalDeviceFeaturesEnabled;

				#define forceEnable(x) check(s.x == VK_TRUE); e.x = VK_TRUE; LOG_GRAPHICS_INFO("Enable '{0}'.", #x);
				{
					// Vulkan 1.0 core.
					forceEnable(core10Features.samplerAnisotropy);
					forceEnable(core10Features.depthClamp);
					forceEnable(core10Features.shaderSampledImageArrayDynamicIndexing);
					forceEnable(core10Features.multiDrawIndirect);
					forceEnable(core10Features.drawIndirectFirstInstance);
					forceEnable(core10Features.independentBlend);
					forceEnable(core10Features.multiViewport);
					forceEnable(core10Features.fragmentStoresAndAtomics);
					forceEnable(core10Features.shaderInt16);
					forceEnable(core10Features.fillModeNonSolid);
					forceEnable(core10Features.depthBiasClamp);

					// Vulkan 1.1 core.
					forceEnable(core11Features.shaderDrawParameters);
					forceEnable(core11Features.uniformAndStorageBuffer16BitAccess);
					forceEnable(core11Features.storageBuffer16BitAccess);

					// Vulkan 1.2 core.
					forceEnable(core12Features.drawIndirectCount);
					forceEnable(core12Features.imagelessFramebuffer);
					forceEnable(core12Features.separateDepthStencilLayouts);
					forceEnable(core12Features.descriptorIndexing);
					forceEnable(core12Features.runtimeDescriptorArray);
					forceEnable(core12Features.descriptorBindingPartiallyBound);
					forceEnable(core12Features.descriptorBindingVariableDescriptorCount);
					forceEnable(core12Features.shaderSampledImageArrayNonUniformIndexing);
					forceEnable(core12Features.descriptorBindingUpdateUnusedWhilePending);
					forceEnable(core12Features.descriptorBindingSampledImageUpdateAfterBind);
					forceEnable(core12Features.descriptorBindingStorageBufferUpdateAfterBind);
					forceEnable(core12Features.shaderStorageBufferArrayNonUniformIndexing);
					forceEnable(core12Features.timelineSemaphore);
					forceEnable(core12Features.bufferDeviceAddress);
					forceEnable(core12Features.shaderFloat16);
					forceEnable(core12Features.storagePushConstant8);
					forceEnable(core12Features.hostQueryReset);
					forceEnable(core12Features.storageBuffer8BitAccess);
					forceEnable(core12Features.uniformAndStorageBuffer8BitAccess);

					// Vulkan 1.3 core.
					forceEnable(core13Features.dynamicRendering);
					forceEnable(core13Features.synchronization2);
					forceEnable(core13Features.maintenance4);

					// EXT dynamic state2.
					forceEnable(extendedDynamicState2Features.extendedDynamicState2LogicOp);

					// EXT dynamic state3
					forceEnable(extendedDynamicState3Features.extendedDynamicState3DepthClampEnable);
					forceEnable(extendedDynamicState3Features.extendedDynamicState3PolygonMode);
				}
				#undef forceEnable

				#define optionalEnable(y, x) if (y) { if (s.x != VK_TRUE) { y = false; } else { e.x = VK_TRUE;  LOG_GRAPHICS_INFO("Enable '{0}'.", #x) } }
				{
					// Raytracing.
					optionalEnable(m_initConfig.bRaytracing, accelerationStructureFeatures.accelerationStructure);
					optionalEnable(m_initConfig.bRaytracing, raytracingPipelineFeatures.rayTracingPipeline);
					optionalEnable(m_initConfig.bRaytracing, rayQueryFeatures.rayQuery);
				}
				#undef optionalEnable
			}

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

			// Prepare queue priority. all 0.5f.
			constexpr auto kQueueDefaultPriority = 0.5f;
			std::vector<float> graphicsQueuePriority(m_gpuQueuesInfo.graphcisQueues.size(), kQueueDefaultPriority);
			std::vector<float> computeQueuePriority(m_gpuQueuesInfo.computeQueues.size(), kQueueDefaultPriority);
			std::vector<float> copyQueuePriority(m_gpuQueuesInfo.copyQueues.size(), kQueueDefaultPriority);
			std::vector<float> sparseBindingQueuePriority(m_gpuQueuesInfo.spatialBindingQueues.size(), kQueueDefaultPriority);
			std::vector<float> videoDecodeQueuePriority(m_gpuQueuesInfo.videoDecodeQueues.size(), kQueueDefaultPriority);

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
				for (auto id = 0; id < m_gpuQueuesInfo.graphcisQueues.size(); id++)
				{
					m_gpuQueuesInfo.graphcisQueues[id].priority = graphicsQueuePriority[id];
				}

				for (auto id = 0; id < m_gpuQueuesInfo.computeQueues.size(); id++)
				{
					m_gpuQueuesInfo.computeQueues[id].priority = computeQueuePriority[id];
				}

				for (auto id = 0; id < m_gpuQueuesInfo.copyQueues.size(); id++)
				{
					m_gpuQueuesInfo.copyQueues[id].priority = copyQueuePriority[id];
				}

				for (auto id = 0; id < m_gpuQueuesInfo.spatialBindingQueues.size(); id++)
				{
					m_gpuQueuesInfo.spatialBindingQueues[id].priority = sparseBindingQueuePriority[id];
				}

				for (auto id = 0; id < m_gpuQueuesInfo.videoDecodeQueues.size(); id++)
				{
					m_gpuQueuesInfo.videoDecodeQueues[id].priority = videoDecodeQueuePriority[id];
				}
			}

			// Build queue create infos.
			{
				VkDeviceQueueCreateInfo queueCreateInfo{ .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };

				if (m_gpuQueuesInfo.graphcisQueues.size() > 0)
				{
					queueCreateInfo.queueFamilyIndex = m_gpuQueuesInfo.graphicsFamily.get();
					queueCreateInfo.queueCount = (uint32)m_gpuQueuesInfo.graphcisQueues.size();
					queueCreateInfo.pQueuePriorities = graphicsQueuePriority.data();
					queueCreateInfos.push_back(queueCreateInfo);
				}

				if (m_gpuQueuesInfo.computeQueues.size() > 0)
				{
					queueCreateInfo.queueFamilyIndex = m_gpuQueuesInfo.computeFamily.get();
					queueCreateInfo.queueCount = (uint32)m_gpuQueuesInfo.computeQueues.size();
					queueCreateInfo.pQueuePriorities = computeQueuePriority.data();
					queueCreateInfos.push_back(queueCreateInfo);
				}

				if (m_gpuQueuesInfo.copyQueues.size() > 0)
				{
					queueCreateInfo.queueFamilyIndex = m_gpuQueuesInfo.copyFamily.get();
					queueCreateInfo.queueCount = (uint32)m_gpuQueuesInfo.copyQueues.size();
					queueCreateInfo.pQueuePriorities = copyQueuePriority.data();
					queueCreateInfos.push_back(queueCreateInfo);
				}

				if (m_gpuQueuesInfo.spatialBindingQueues.size() > 0)
				{
					queueCreateInfo.queueFamilyIndex = m_gpuQueuesInfo.sparseBindingFamily.get();
					queueCreateInfo.queueCount = (uint32)m_gpuQueuesInfo.spatialBindingQueues.size();
					queueCreateInfo.pQueuePriorities = sparseBindingQueuePriority.data();
					queueCreateInfos.push_back(queueCreateInfo);
				}

				if (m_gpuQueuesInfo.videoDecodeQueues.size() > 0)
				{
					queueCreateInfo.queueFamilyIndex = m_gpuQueuesInfo.videoDecodeFamily.get();
					queueCreateInfo.queueCount = (uint32)m_gpuQueuesInfo.videoDecodeQueues.size();
					queueCreateInfo.pQueuePriorities = videoDecodeQueuePriority.data();
					queueCreateInfos.push_back(queueCreateInfo);
				}
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
			void** pNextDeviceCreateInfo = m_physicalDeviceFeaturesEnabled.stepNextPtr(physicalDeviceFeatures2);

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
			checkVkResult(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));
		}

		// Post device create process.
		{
			volkLoadDevice(m_device);

			// Queue udpate.
			{
				for (auto id = 0; id < m_gpuQueuesInfo.graphcisQueues.size(); id++)
				{
					vkGetDeviceQueue(m_device, m_gpuQueuesInfo.graphicsFamily.get(), id, &m_gpuQueuesInfo.graphcisQueues[id].queue);
				}
				for (auto id = 0; id < m_gpuQueuesInfo.computeQueues.size(); id++)
				{
					vkGetDeviceQueue(m_device, m_gpuQueuesInfo.computeFamily.get(), id, &m_gpuQueuesInfo.computeQueues[id].queue);
				}
				for (auto id = 0; id < m_gpuQueuesInfo.copyQueues.size(); id++)
				{
					vkGetDeviceQueue(m_device, m_gpuQueuesInfo.copyFamily.get(), id, &m_gpuQueuesInfo.copyQueues[id].queue);
				}
				for (auto id = 0; id < m_gpuQueuesInfo.spatialBindingQueues.size(); id++)
				{
					vkGetDeviceQueue(m_device, m_gpuQueuesInfo.sparseBindingFamily.get(), id, &m_gpuQueuesInfo.spatialBindingQueues[id].queue);
				}
				for (auto id = 0; id < m_gpuQueuesInfo.videoDecodeQueues.size(); id++)
				{
					vkGetDeviceQueue(m_device, m_gpuQueuesInfo.videoDecodeFamily.get(), id, &m_gpuQueuesInfo.videoDecodeQueues[id].queue);
				}
			}
		}


		return true;
	}

	void Context::beforeRelease()
	{

	}


	void Context::onRelease()
	{
		if (m_device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_device, nullptr);
		}

		if (m_debugUtilsHandle != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugUtilsHandle, nullptr);
			m_debugUtilsHandle = VK_NULL_HANDLE;
		}

		if (m_instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_instance, nullptr);
			m_instance = VK_NULL_HANDLE;
		}
	}
}

namespace chord
{
	graphics::Context& getContext()
	{
		return Application::get().getSubsystem<graphics::Context>();
	}
}

