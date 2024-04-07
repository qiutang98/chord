#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>

namespace chord
{
	enum class ELogType : uint8
	{
		None = 0,

		Trace,
		Info,
		Warn,
		Error,
		Fatal,
		Other,

		MAX,
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

		void updateLogFile();

	private:
		std::shared_ptr<spdlog::sinks::basic_file_sink_mt> m_fileSink = nullptr;
		std::shared_ptr<spdlog::sinks::dist_sink_mt> m_fileDestSink = nullptr;

		// Sink cache all logger.
		std::vector<spdlog::sink_ptr> m_logSinks{ };

		// Default logger.
		std::shared_ptr<spdlog::logger> m_defaultLogger;

		// Logger cache for custom logger.
		std::shared_ptr<LogCacheSink<std::mutex>> m_loggerCache;
	};

	#define LOG_TRACE(...) chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().getDefaultLogger()->trace(__VA_ARGS__); })
	#define LOG_INFO(...) chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().getDefaultLogger()->info(__VA_ARGS__); })
	#define LOG_WARN(...) chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().getDefaultLogger()->warn(__VA_ARGS__); })
	#define LOG_ERROR(...) chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().getDefaultLogger()->error(__VA_ARGS__); })
	#define LOG_FATAL(...) chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().getDefaultLogger()->critical(__VA_ARGS__); chord::applicationCrash(); })
}

#define check(x) chord_macro_sup_checkPrintContent(x, LOG_FATAL)
#define checkMsgf(x, ...) chord_macro_sup_checkMsgfPrintContent(x, LOG_FATAL, __VA_ARGS__)

// Only call once, will trigger break.
#define ensureMsgf(x, ...) chord_macro_sup_ensureMsgfContent(x, LOG_ERROR, __VA_ARGS__)

// Used for check unexpected entry.
#define checkEntry() chord_macro_sup_checkEntryContent(LOG_FATAL)

// Used for unimplement check or crash.
#define unimplemented() chord_macro_sup_unimplementedContent(LOG_FATAL)