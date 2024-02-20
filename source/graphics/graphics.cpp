#include "graphics.h"

namespace chord::graphics
{
	spdlog::logger& log::get()
	{
		static auto logger = LoggerSystem::get().registerLogger("Graphics");
		return *logger;
	}

	inline static void dynamicDispatcherInit(const vk::Device& device, const vk::Instance& instance)
	{
		auto& defaultDispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;

		const vk::DynamicLoader dl;
		const PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = 
			dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");

		// Init default dynamic loader dispatcher.
		defaultDispatcher.init(vkGetInstanceProcAddr);
		defaultDispatcher.init(instance);
		defaultDispatcher.init(device);
	}
}


