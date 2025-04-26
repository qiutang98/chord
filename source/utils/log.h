#pragma once

#include <utils/utils.h>
#include <utils/noncopyable.h>
#include <utils/delegate.h>

namespace chord
{
	enum class ELogLevel : uint8
	{
		Trace = 0,
		Info,
		Warn,
		Error,
		Fatal,

		COUNT
	};

	class AsyncLogWriter;
	class LoggerSystem : NonCopyable
	{
	private:
		std::atomic<bool> m_asyncLogWriterAlreadyCreate { false };
		mutable std::mutex m_asyncLogWriterCreateMutex;
		AsyncLogWriter* m_asyncLogWriter{ nullptr };

		//
		bool m_bStdOutput = true;
		ELogLevel m_minLogLevel = ELogLevel::Trace;

		//
		ChordEvent<const std::string&, ELogLevel> m_logCallback;

	public:
		static LoggerSystem& get();
		~LoggerSystem();

		// From any thread.
		void addLog(const std::string& loggerName, const std::string& message, ELogLevel level);

		// Push callback to logger sink.
		CHORD_NODISCARD EventHandle pushCallback(std::function<void(const std::string&, ELogLevel)>&& callback)
		{
			return m_logCallback.add(std::move(callback));
		}

		// Pop callback from logger sink.
		void popCallback(EventHandle& handle)
		{
			const bool bResult = m_logCallback.remove(handle);
			assert(bResult);
		}

		static void cleanDiskSavedLogFile(int32 keepDays, const std::filesystem::path& folerPath = {});

		void trace(const std::string& loggerName, const std::string& message) { addLog(loggerName, message, ELogLevel::Trace); }
		void info (const std::string& loggerName, const std::string& message) { addLog(loggerName, message, ELogLevel::Info); }
		void warn (const std::string& loggerName, const std::string& message) { addLog(loggerName, message, ELogLevel::Warn); }
		void error(const std::string& loggerName, const std::string& message) { addLog(loggerName, message, ELogLevel::Error); }
		void fatal(const std::string& loggerName, const std::string& message) { addLog(loggerName, message, ELogLevel::Fatal); }

		inline void updateLoggerWriterAnyThread()
		{
			updateLoggerWriter(true);
		}

	private:
		LoggerSystem() = default;
		void createLoggerIfNoExist();

		void updateLoggerWriter(bool bAnyThread = true);
	};
}

#define LOG_TRACE(...) chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().trace("Default", std::format(__VA_ARGS__)); })
#define LOG_INFO(...)  chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().info ("Default", std::format(__VA_ARGS__)); })
#define LOG_WARN(...)  chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().warn ("Default", std::format(__VA_ARGS__)); })
#define LOG_ERROR(...) chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().error("Default", std::format(__VA_ARGS__)); })
#define LOG_FATAL(...) chord_macro_sup_enableLogOnly({ chord::LoggerSystem::get().fatal("Default", std::format(__VA_ARGS__)); CHORD_CRASH })

#define check(x) chord_macro_sup_checkPrintContent(x, LOG_FATAL)
#define checkMsgf(x, ...) chord_macro_sup_checkMsgfPrintContent(x, LOG_FATAL, __VA_ARGS__)

// Only call once, will trigger break.
#define ensureMsgf(x, ...) chord_macro_sup_ensureMsgfContent(x, LOG_ERROR, __VA_ARGS__)

// Used for check unexpected entry.
#define checkEntry() chord_macro_sup_checkEntryContent(LOG_FATAL)

// Used for unimplement check or crash.
#define unimplemented() chord_macro_sup_unimplementedContent(LOG_FATAL)