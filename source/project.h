#pragma once
#include <utils/utils.h>

namespace chord
{
	struct ProjectPath
	{
		// Project stem name, not include period and suffix.
		u16str projectName;

		// Project file absolute path file in this computer, this is runtime generate value.
		u16str projectFilePath;

		// Misc project folder path.
		u16str rootPath;
		u16str assetPath;
		u16str configPath;
		u16str logPath;
		u16str cachePath;
	};

	class Project 
	{
	private:
		explicit Project() = default;

	private:
		ProjectPath m_projectPath;

		// Current project setup or not.
		bool m_bSetup = false;

	public:
		static Project& get();

		void setup(const std::filesystem::path& inProjectPath);

		const ProjectPath& getPath() const 
		{
			return m_projectPath; 
		}

		bool isSetup() const
		{
			return m_bSetup;
		}

		std::string getAppTitleName() const;
	};
}