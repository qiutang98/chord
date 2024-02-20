#include "log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <string>
#include <filesystem>
#include <iostream>

#include <utils/delegate.h>
#include <utils/cvar.h>

namespace chord
{
	AutoCVar<std::string> cVarLogPrintFormat("r.log.printFormat", "%^[%H:%M:%S][%l] %n: %v%$", "Print format of log in app.", EConsoleVarFlags::ReadOnly);
	AutoCVar<bool> cVarLogFile("r.log.file", true, "Enable log file save in disk.", EConsoleVarFlags::ReadOnly);
	AutoCVar<std::string> cVarLogFileFormat("r.log.file.format", "[%H:%M:%S][%l] %n: %v", "Saved format of log in file.", EConsoleVarFlags::ReadOnly);
	AutoCVar<std::string> cVarLogFileOutputFolder("r.log.file.folder", "save/log", "Save folder path of log file.", EConsoleVarFlags::ReadOnly);
	AutoCVar<std::string> cVarLogFileName("r.log.file.name", "chord", "Save name of log file.", EConsoleVarFlags::ReadOnly);


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
	private:
		Events<const std::string&, ELogType> m_callbacks;

		static ELogType toLogType(spdlog::level::level_enum level)
		{
			switch (level)
			{
			case spdlog::level::trace:
				return ELogType::Trace;
			case spdlog::level::info:
				return ELogType::Info;
			case spdlog::level::warn:
				return ELogType::Warn;
			case spdlog::level::err:
				return ELogType::Error;
			case spdlog::level::critical:
				return ELogType::Fatal;
			default:
				return ELogType::Other;
			}
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
		CHECK(m_loggerCache->m_callbacks.remove(handle));
	}

	LoggerSystem::LoggerSystem()
	{
		// Basic sinks.
		m_logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

		// Cache sinks.
		m_logSinks.emplace_back(std::make_shared<LogCacheSink<std::mutex>>()); 		

		// Set format.
		for (auto& sink : m_logSinks)
		{
			sink->set_pattern(cVarLogPrintFormat.get());
		}

		if (cVarLogFile.get())
		{
			using TimePoint = std::chrono::system_clock::time_point;
			auto serializeTimePoint = [](const TimePoint& time, const std::string& format)
			{
				std::time_t tt = std::chrono::system_clock::to_time_t(time);
				std::tm tm = *std::localtime(&tt);
				std::stringstream ss;
				ss << std::put_time(&tm, format.c_str());
				return ss.str();
			};

			TimePoint input = std::chrono::system_clock::now();
			const auto& saveFolder = cVarLogFileOutputFolder.get();
			if (!std::filesystem::exists(saveFolder))
			{
				std::filesystem::create_directories(saveFolder);
			}

			{
				auto saveFilePath = cVarLogFileName.get() + serializeTimePoint(input, "_%Y_%m_%d_%H_%M_%S") + ".log";
				auto finalPath = std::filesystem::path(saveFolder) / saveFilePath;

				m_logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(finalPath.string().c_str(), true));
				m_logSinks.back()->set_pattern(cVarLogFileFormat.get());
			}
		}

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