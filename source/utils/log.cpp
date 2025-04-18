#include <utils/log.h>

#include <string>
#include <filesystem>
#include <iostream>

#include <utils/delegate.h>
#include <utils/cvar.h>
#include <regex>
#include <execution>

namespace chord
{
	static u16str GLogPrintFormat = u16str("%^[%H:%M:%S][%l] %n: %v%$");
	static AutoCVarRef cVarLogPrintFormat(
		"r.log.printFormat", 
		GLogPrintFormat,
		"Print format of log in app.", 
		EConsoleVarFlags::ReadOnly);

	static bool bGLogFile = true;
	static AutoCVarRef cVarLogFile(
		"r.log.file", 
		bGLogFile,
		"Enable log file save in disk.", 
		EConsoleVarFlags::ReadOnly);

	static bool bGLogFileDelete = true;
	static AutoCVarRef cVarLogFileDelete(
		"r.log.file.delete", 
		bGLogFileDelete,
		"Enable delete old log file save in disk.", 
		EConsoleVarFlags::ReadOnly);

	static int32 GLogFileDeleteDay = 2;
	static AutoCVarRef cVarLogFileDeleteDay(
		"r.log.file.deleteDay", 
		GLogFileDeleteDay,
		"Delete days for old logs.", 
		EConsoleVarFlags::ReadOnly);

	static u16str GLogFileFormat = u16str("[%H:%M:%S][%l] %n: %v");
	static AutoCVarRef cVarLogFileFormat(
		"r.log.file.format", 
		GLogFileFormat,
		"Saved format of log in file.", 
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

	LoggerSystem& LoggerSystem::get()
	{
		static LoggerSystem logger { };
		return logger;
	}

	// Custom log cache sink, use for editor/hub/custom console output. etc.
	template<typename Mutex>
	class LogCacheSink : public spdlog::sinks::base_sink <Mutex>
	{
		friend LoggerSystem;
	public:
		virtual ~LogCacheSink() { }

	private:
		Events<LogCacheSink, const std::string&, ELogType> m_callbacks;

		static ELogType toLogType(spdlog::level::level_enum level)
		{
			switch (level)
			{
			case spdlog::level::trace:    return ELogType::Trace;
			case spdlog::level::info:     return ELogType::Info;
			case spdlog::level::warn:     return ELogType::Warn;
			case spdlog::level::err:      return ELogType::Error;
			case spdlog::level::critical: return ELogType::Fatal;
			}
			return ELogType::Other;
		}

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override
		{
			spdlog::memory_buf_t formatted;
			spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
			m_callbacks.broadcast(fmt::to_string(formatted), toLogType(msg.level));
		}

		void flush_() override
		{

		}
	};

	EventHandle LoggerSystem::pushCallback(std::function<void(const std::string&, ELogType)>&& callback)
	{
		return m_loggerCache->m_callbacks.add(std::move(callback));
	}

	void LoggerSystem::popCallback(EventHandle& handle)
	{
		check(m_loggerCache->m_callbacks.remove(handle));
	}

	void LoggerSystem::updateLogFile()
	{
		// Create save folder for log if no exist.
		const auto saveFolder = GLogFileOutputFolder.u16();
		if (!std::filesystem::exists(saveFolder))
		{
			std::filesystem::create_directories(saveFolder);
		}

		using TimePoint = std::chrono::system_clock::time_point;
		auto serializeTimePoint = [](const TimePoint& time, const std::string& format)
		{
			std::time_t tt = std::chrono::system_clock::to_time_t(time);
			std::tm tm = *std::localtime(&tt);
			std::stringstream ss;
			ss << std::put_time(&tm, format.c_str());
			return ss.str();
		};

		TimePoint now = std::chrono::system_clock::now();

		const std::regex kLogNamePattern(R"(\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2})");
		auto shouldDeleteLogInDisk = [&](const std::filesystem::path& path) -> bool
		{
			if (path.extension() == ".log")
			{
				const auto fileName = path.stem().string();

				std::smatch matches;
				if (std::regex_search(fileName, matches, kLogNamePattern))
				{
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
					if (days >= GLogFileDeleteDay)
					{
						return true;
					}
				}
			}

			return false;
		};

		const auto saveFilePath = GLogFileName.u16() + u16str(serializeTimePoint(now, "_%Y_%m_%d_%H_%M_%S") + ".log").u16();
		const auto finalPath = std::filesystem::path(saveFolder) / saveFilePath;

		// Delete old log files out of day.
		if (bGLogFileDelete)
		{
			std::vector<std::filesystem::path> pendingFiles = {};

			auto checkDeleted = finalPath.parent_path();
			if (std::filesystem::exists(checkDeleted))
			{
				for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(checkDeleted))
				{
					const auto& path = dirEntry.path();
					if (shouldDeleteLogInDisk(path))
					{
						pendingFiles.push_back(path);
					}
				}
			}

			// Parallel delete.
			std::for_each(std::execution::par, pendingFiles.begin(), pendingFiles.end(), [](const auto& p) { std::filesystem::remove(p); });
		}

		// Create new log file.
		if (bGLogFile)
		{
			{
				const auto name = finalPath.stem().string();
				check(std::regex_search(name, kLogNamePattern));
			}

			m_fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(finalPath.string().c_str(), true);
			m_fileSink->set_pattern(GLogFileFormat.str());

			if (m_fileDestSink)
			{
				std::vector<spdlog::sink_ptr> nextSinks({ m_fileSink });
				m_fileDestSink->set_sinks(nextSinks);
			}
			else
			{
				std::vector<spdlog::sink_ptr> initialSinks({ m_fileSink });
				m_fileDestSink = std::make_shared<spdlog::sinks::dist_sink_mt>(initialSinks);
				m_logSinks.push_back(m_fileDestSink);
			}

		}
	}

	LoggerSystem::LoggerSystem()
	{
		// Basic sinks.
		m_logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

		// Cache sinks.
		m_loggerCache = std::make_shared<LogCacheSink<std::mutex>>();
		m_logSinks.emplace_back(m_loggerCache);

		// Set format.
		for (auto& sink : m_logSinks)
		{
			sink->set_pattern(GLogPrintFormat.str());
		}

		// Log file.
		updateLogFile();

		// Register a default logger for basic usage.
		m_defaultLogger = registerLogger("Default");
	}

	std::shared_ptr<spdlog::logger> LoggerSystem::registerLogger(const std::string& name)
	{
		auto logger = std::make_shared<spdlog::logger>(name, begin(m_logSinks), end(m_logSinks));
		spdlog::register_logger(logger);

		logger->set_level(spdlog::level::trace);
		logger->flush_on(spdlog::level::trace);

		return logger;
	}
}