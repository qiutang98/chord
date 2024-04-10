#include <project.h>
#include <fstream>
#include <utils/utils.h>
#include <utils/cvar.h>
#include <utils/log.h>
#include <application/application.h>

namespace chord
{
	Project& Project::get()
	{
		static Project project{};
		return project;
	}

	void Project::setup(const std::filesystem::path& inProjectPath)
	{
		if (!std::filesystem::exists(inProjectPath))
		{
			std::ofstream os(inProjectPath);
		}

		m_projectPath.projectFilePath = inProjectPath.u16string();
		m_projectPath.projectName     = inProjectPath.filename().replace_extension().u16string();

		m_projectPath.rootPath        = inProjectPath.parent_path().u16string();
		m_projectPath.assetPath       = (inProjectPath.parent_path() / "Asset").u16string();
		m_projectPath.cachePath       = (inProjectPath.parent_path() / "Cache").u16string();
		m_projectPath.configPath      = (inProjectPath.parent_path() / "Config").u16string();

		auto logFolderPath = (inProjectPath.parent_path() / "Log");
		m_projectPath.logPath         = logFolderPath.u16string();

		auto createDirectoryIfNoExist = [](const std::u16string& path)
		{
			if (!std::filesystem::exists(path)) std::filesystem::create_directory(path);
		};

		createDirectoryIfNoExist(m_projectPath.assetPath.u16());
		createDirectoryIfNoExist(m_projectPath.cachePath.u16());
		createDirectoryIfNoExist(m_projectPath.configPath.u16());
		createDirectoryIfNoExist(m_projectPath.logPath.u16());
		
		CVarSystem::get().getCVarCheck<std::string>("r.log.file.folder")->set(logFolderPath.string());
		CVarSystem::get().getCVarCheck<std::string>("r.log.file.name")->set("flower");
		LoggerSystem::get().updateLogFile();

		const auto titleName = getAppTitleName();
		glfwSetWindowTitle(Application::get().getWindowData().window, titleName.c_str());

		// Final update setup state.
		m_bSetup = true;
	}

	std::string Project::getAppTitleName() const
	{
		return std::format("{} - {}", Application::get().getName(), m_projectPath.projectName.u8());
	}
}

