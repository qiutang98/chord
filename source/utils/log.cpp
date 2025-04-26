#include <utils/log.h>

#include <string>
#include <filesystem>
#include <iostream>

#include <utils/delegate.h>
#include <utils/cvar.h>
#include <utils/mpsc_queue.h>

namespace chord
{
	static bool bGLogFile = true;
	static AutoCVarRef cVarLogFile(
		"r.log.file", 
		bGLogFile,
		"Enable log file save in disk.", 
		EConsoleVarFlags::ReadOnly);

	static u16str GLogFileOutputFolder = u16str("save/log");
	static AutoCVarRef cVarLogFileOutputFolder(
		"r.log.file.folder", 
		GLogFileOutputFolder,
		"Save folder path of log file.",
		EConsoleVarFlags::ReadOnly);

	static u16str GLogFileName = u16str("chord");
	static AutoCVarRef cVarLogFileName(
		"r.log.file.name", 
		GLogFileName,
		"Save name of log file.", 
		EConsoleVarFlags::ReadOnly);

	// 
	static const std::regex kLogNamePattern(R"(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})");

	LoggerSystem& LoggerSystem::get()
	{
		static LoggerSystem logger { };
		return logger;
	}

	struct LogDescriptor
	{
		const char* name;
		const char* color;
	};

	static const LogDescriptor kLevelStr[(uint32)ELogLevel::COUNT] =
	{
		{ .name = "TRACE", .color = " \x1b[37;1m"},
		{ .name = "INFO",  .color = " \x1b[32;1m"},
		{ .name = "WARN",  .color = " \x1b[33;1m"},
		{ .name = "ERROR", .color = " \x1b[31;1m"},
		{ .name = "FATAL", .color = " \x1b[35;1m"},
	};

	class AsyncLogWriter
	{
	private:
		bool m_bStdOut;
		ELogLevel m_minLogLevel;

		struct LogMessage
		{
			std::string message;
			ELogLevel level;
		};
		// 4kb page -> 73 element.
		MPSCQueue<LogMessage, MPSCQueuePoolAllocator<LogMessage, 4096>> m_messageQueue;

		// Main thread.
		std::future<void> m_future;

		std::mutex mutex_pushing;
		std::condition_variable cv_pushing;

		alignas(kCpuCachelineSize) std::atomic<bool> m_bRunning;

		// Writing log file.
		FILE* m_file;

	private:
		void printLog(const LogMessage& message)
		{
			if (m_bStdOut)
			{
				printf(std::format("{1}{0}\x1b[0m \n", message.message, kLevelStr[(uint32)message.level].color).c_str());
			}

			if (m_file)
			{
				fprintf(m_file, "%s\n", message.message.c_str());
				fflush(m_file); // One log one fflush.
			}
		}

	public:
		AsyncLogWriter(bool bStdOut, ELogLevel minLogLevel, const std::filesystem::path& outFilePath)
			: m_bStdOut(bStdOut)
			, m_minLogLevel(minLogLevel)
			, m_bRunning(true)
			, m_file(nullptr)
		{
			if (!outFilePath.empty())
			{
				m_file = fopen(outFilePath.string().c_str(), "a");
			}

			m_future = std::async(std::launch::async, [this]()
			{
				LogMessage logMessage;

				auto printAllLogs = [&]()
				{
					while (!m_messageQueue.isEmpty())
					{
						if (m_messageQueue.dequeue(logMessage))
						{
							printLog(logMessage);
						}
						else
						{
							std::this_thread::yield();
						}
					}
				};

				while (m_bRunning)
				{
					printAllLogs();

					// Make current thread wait for notify.
					{
						std::unique_lock lock(mutex_pushing);
						cv_pushing.wait(lock);
					}
				}

				// Flush all log message before return.
				printAllLogs();
			});
		}

		bool shouldPrintLog(ELogLevel level) const
		{
			return level >= m_minLogLevel;
		}

		void log(std::string&& message, ELogLevel level)
		{
			LogMessage logMessage;
			logMessage.level = level;
			logMessage.message = std::move(message);

			// Enqueue.
			m_messageQueue.enqueue(std::move(logMessage));

			// Notify all thread break while loop.
			cv_pushing.notify_one();
		}

		~AsyncLogWriter()
		{
			m_bRunning = false;
			cv_pushing.notify_all();

			//
			m_future.wait();

			if (m_file)
			{
				fclose(m_file);
			}
		}
	};

	LoggerSystem::~LoggerSystem()
	{
		if (m_asyncLogWriter != nullptr)
		{
			delete m_asyncLogWriter;
			m_asyncLogWriter = nullptr;
		}
	}

	static bool shouldDeleteOldLogInDisk(const std::filesystem::path& path, const std::chrono::system_clock::time_point& now, int32 maxKeepDay)
	{
		if (path.extension() != ".log")
		{
			return false; // Early return when extension no match.
		}

		const auto fileName = path.stem().string();

		std::smatch matches;
		if (!std::regex_search(fileName, matches, kLogNamePattern))
		{
			return false; // Early return when file name format no match.
		}

		std::string datetimeStr = matches[0];
		int32 year, month, day, hour, minute, second;
		{
			const auto item = sscanf(datetimeStr.c_str(), "%d_%d_%d_%d_%d_%d", &year, &month, &day, &hour, &minute, &second);
			(void)item;
		}

		std::tm tm = { 0 };
		tm.tm_year = year - 1900;
		tm.tm_mon = month - 1;
		tm.tm_mday = day;
		tm.tm_hour = hour;
		tm.tm_min = minute;
		tm.tm_sec = second;

		std::time_t t = std::mktime(&tm);
		std::chrono::system_clock::time_point timePoint = std::chrono::system_clock::from_time_t(t);

		auto duration = now - timePoint;
		auto days = std::chrono::duration_cast<std::chrono::days>(duration).count();

		// Now get final result.
		return days > maxKeepDay;
	}

	void LoggerSystem::cleanDiskSavedLogFile(int32 keepDays, const std::filesystem::path& folerPath)
	{
		std::vector<std::filesystem::path> pendingFiles = {};
		auto now = std::chrono::system_clock::now();

		std::filesystem::path checkPath = folerPath.empty() ? GLogFileOutputFolder.u16() : folerPath;
		if (std::filesystem::exists(folerPath))
		{
			for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(folerPath))
			{
				const auto& path = dirEntry.path();
				if (shouldDeleteOldLogInDisk(path, now, keepDays))
				{
					pendingFiles.push_back(path);
				}
			}
		}

		// Parallel delete.
		std::for_each(std::execution::par, pendingFiles.begin(), pendingFiles.end(), [](const auto& p) { std::filesystem::remove(p); });
	}

	void LoggerSystem::updateLoggerWriter(bool bAnyThread)
	{
		std::unique_lock<std::mutex> lock;
		if (bAnyThread)
		{
			lock = std::unique_lock(m_asyncLogWriterCreateMutex);
		}

		if (!bGLogFile)
		{
			if (m_asyncLogWriter)
			{
				delete m_asyncLogWriter;
				m_asyncLogWriter = nullptr;
			}
			return;
		}

		// Create save folder for log if no exist.
		const std::u16string& saveFolder = GLogFileOutputFolder.u16();
		if (!std::filesystem::exists(saveFolder))
		{
			std::filesystem::create_directories(saveFolder);
		}

		auto now = std::chrono::system_clock::now();

		//
		const auto saveFilePath = GLogFileName.u16() + u16str(formatTimestamp(now, "_%Y_%m_%d_%H_%M_%S") + ".log").u16();
		const auto finalPath = std::filesystem::path(saveFolder) / saveFilePath;

		if constexpr (CHORD_DEBUG)
		{
			const auto name = finalPath.stem().string();
			assert(std::regex_search(name, kLogNamePattern) && "Log name pattern must match kLogNamePattern!");
		}

		m_asyncLogWriter = new AsyncLogWriter(m_bStdOutput, m_minLogLevel, finalPath);
	
		//
		std::atomic_thread_fence(std::memory_order_seq_cst);
	}

	// First time create logger.
	void LoggerSystem::createLoggerIfNoExist()
	{
		if (m_asyncLogWriterAlreadyCreate.load()) CHORD_LIKELY
		{
			return;
		}

		std::lock_guard lock(m_asyncLogWriterCreateMutex);
		if (m_asyncLogWriter == nullptr) CHORD_UNLIKELY
		{
			updateLoggerWriter(false); // Outside lock so don't lock inside.
		}

		m_asyncLogWriterAlreadyCreate.store(true);

		std::atomic_thread_fence(std::memory_order_seq_cst);
	}

	// From any thread.
	void LoggerSystem::addLog(const std::string& loggerName, const std::string& message, ELogLevel level)
	{
		createLoggerIfNoExist();
		if (m_asyncLogWriter && m_asyncLogWriter->shouldPrintLog(level))
		{
			const auto& desc = kLevelStr[(uint32)level];

			//
			auto now = std::chrono::system_clock::now();
			std::string finalLogStr = std::format("{2} [{0}] {3}: {1}", desc.name, message, formatTimestamp(now, "%m-%d %H:%M:%S"), loggerName);
			m_logCallback.broadcast(finalLogStr, level);

			// 
			m_asyncLogWriter->log(std::move(finalLogStr), level);
		}
	}
}