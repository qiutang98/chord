#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace chord
{
	enum class ELogType : uint8
	{
		Trace = 0,
		Info,
		Warn,
		Error,
		Fatal,
		Other,

		Max,
	};

	// Custom log cache sink.
	template<typename Mutex> class LogCacheSink;
	class EventHandle;

	class LoggerSystem : NonCopyable
	{
	private:
		LoggerSystem();

	public:
		static LoggerSystem& get();

		// Get default logger.
		auto& getDefaultLogger() noexcept { return m_defaultLogger; }

		// Register a new logger.
		CHORD_NODISCARD std::shared_ptr<spdlog::logger> registerLogger(const std::string& name);

		// Push callback to logger sink.
		CHORD_NODISCARD EventHandle pushCallback(std::function<void(const std::string&, ELogType)>&& callback);

		// Pop callback from logger sink.
		void popCallback(EventHandle& name);

	private:
		// Sink cache all logger.
		std::vector<spdlog::sink_ptr> m_logSinks{ };

		// Default logger.
		std::shared_ptr<spdlog::logger> m_defaultLogger;

		// Logger cache for custom logger.
		std::shared_ptr<LogCacheSink<std::mutex>> m_loggerCache;
	};
}

#ifdef ENABLE_LOG
	#define LOG_TRACE(...) { ::chord::LoggerSystem::get().getDefaultLogger()->trace   (__VA_ARGS__); }
	#define LOG_INFO(...)  { ::chord::LoggerSystem::get().getDefaultLogger()->info    (__VA_ARGS__); }
	#define LOG_WARN(...)  { ::chord::LoggerSystem::get().getDefaultLogger()->warn    (__VA_ARGS__); }
	#define LOG_ERROR(...) { ::chord::LoggerSystem::get().getDefaultLogger()->error   (__VA_ARGS__); }
	#define LOG_FATAL(...) { ::chord::LoggerSystem::get().getDefaultLogger()->critical(__VA_ARGS__); ::chord::applicationCrash(); }
#else
	#define LOG_TRACE(...)   
	#define LOG_INFO (...)    
	#define LOG_WARN(...)   
	#define LOG_ERROR(...)    
	#define LOG_FATAL(...) { ::chord::applicationCrash(); }
#endif

#if CHORD_DEBUG
#define CHECK(x) { if(!(x)) { LOG_FATAL("Check failed in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__); chord::debugBreak(); } }
#define ASSERT(x, ...) { if(!(x)) { LOG_FATAL("Assert failed with message: '{3}' in function '{1}' at line {0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__, std::format(__VA_ARGS__)); chord::debugBreak(); } }
#else
#define CHECK(x) { if(!(x)) { LOG_FATAL("Check error."); } }
#define ASSERT(x, ...) { if(!(x)) { LOG_FATAL("Assert failed: {0}.", __VA_ARGS__); } }
#endif

// Only call once, will trigger break.
#define ENSURE(x, ...) { static bool b = false; if(!b && !(x)) { b = true; LOG_ERROR("Ensure failed with message '{3}' in function '{1}' at line #{0} on file '{2}'.", __LINE__, __FUNCTION__, __FILE__, std::format(__VA_ARGS__)); chord::debugBreak(); } }

// Used for CHECK unexpected entry.
#define CHECK_ENTRY() ASSERT(false, "Should not entry here, fix me!")

// Used for unimplement CHECK or crash.
#define UNIMPLEMENT() ASSERT(false, "Un-implement yet, fix me!")