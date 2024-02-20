#pragma once

#include <utils/log.h>

namespace chord::graphics
{
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
	#define LOG_GRAPHICS_FATAL(...) { ::chord::graphics::log::get().critical(__VA_ARGS__); throw std::runtime_error("Utils graphics fatal!"); }
#else
	#define LOG_GRAPHICS_TRACE(...)   
	#define LOG_GRAPHICS_INFO(...)    
	#define LOG_GRAPHICS_WARN(...)   
	#define LOG_GRAPHICS_ERROR(...)    
	#define LOG_GRAPHICS_FATAL(...) { throw std::runtime_error("Utils graphics fatal!"); }
#endif

#ifdef APP_DEBUG
	#define CHECK_GRAPHICS(x) { if(!(x)) { LOG_GRAPHICS_FATAL("Check error, at line {0} on file {1}.", __LINE__, __FILE__); DEBUG_BREAK(); } }
	#define ASSERT_GRAPHICS(x, ...) { if(!(x)) { LOG_GRAPHICS_FATAL("Assert failed: '{2}', at line {0} on file {1}.", __LINE__, __FILE__, std::format(__VA_ARGS__)); DEBUG_BREAK(); } }
#else
	#define CHECK_GRAPHICS(x) { if(!(x)) { LOG_GRAPHICS_FATAL("Check error."); } }
	#define ASSERT_GRAPHICS(x, ...) { if(!(x)) { LOG_GRAPHICS_FATAL("Assert failed: {0}.", __VA_ARGS__); } }
#endif

#define ENSURE_GRAPHICS(x, ...) { static bool bExecuted = false; if((!bExecuted) && !(x)) { bExecuted = true; LOG_GRAPHICS_ERROR("Ensure failed in graphics: '{2}', at line {0} on file {1}.", __LINE__, __FILE__, std::format(__VA_ARGS__)); DEBUG_BREAK(); } }
