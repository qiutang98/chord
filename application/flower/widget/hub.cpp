#include "hub.h"
#include "../flower.h"

#include <ui/ui_helper.h>
#include <fstream>
#include <nfd.h>
#include <project.h>
#include <utfcpp/utf8.h>
#include <utfcpp/utf8/cpp17.h>

using namespace chord;
using namespace chord::graphics;

HubWidget::HubWidget() : IWidget("flower projects", "flower projects")
{
	m_bShow = false;
}

void HubWidget::onInit()
{
	m_recentProjectList.load();
}

void HubWidget::onRelease()
{
	m_recentProjectList.save();
}

void HubWidget::onTick(const ApplicationTickData& tickData)
{
	u16str projectPath {};

	ImGui::DockSpaceOverViewport();

	static bool bUseWorkArea = true;
	static ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	ImGui::SetNextWindowPos(bUseWorkArea ? viewport->WorkPos : viewport->Pos);
	ImGui::SetNextWindowSize(bUseWorkArea ? viewport->WorkSize : viewport->Size);

	const float footerHeightToReserve =
		ImGui::GetStyle().ItemSpacing.y +
		ImGui::GetFrameHeightWithSpacing();

	ImGui::Begin("ProjectSelectedWindow", &bUseWorkArea, flags);
	{
		ImGui::Indent();

		ImGui::Spacing();
		ImGui::TextDisabled("Develop by qiutang under MIT copyright, feedback and contact with qiutanguu@gmail.com.");

		ImGui::Spacing();
		ImGui::TextDisabled("Dark game engine version 0.0.1 alpha test, project select or create...");

		ImGui::NewLine();

		static std::string loadName = utf8::utf16to8(u"加载");
		if (ImGui::Button(loadName.c_str(), {4.0f * ImGui::GetFontSize(), 0.0f}))
		{
			loadProject();
		}
		ImGui::SameLine();

		static std::string newName = utf8::utf16to8(u"新建");
		if (ImGui::Button(newName.c_str(), {4.0f * ImGui::GetFontSize(), 0.0f}))
		{
			newProject();
		}
		ImGui::SameLine();

		ImGui::BeginDisabled();
		ImGui::InputText(" ", m_projectPath, kProjectPathSize); ImGui::SameLine();
		ImGui::EndDisabled();

		static std::string ensureName = utf8::utf16to8(u"确认");
		ui::disableLambda([&]()
		{
			if (ImGui::Button(ensureName.c_str(), {3.0f * ImGui::GetFontSize(), 0.0f}))
			{
				projectPath = u16str(m_projectPath);
			}
		}, !m_bProjectPathReady);

		ImGui::NewLine();
		ImGui::Separator();

		ImGui::Spacing();
		ImGui::TextDisabled("History project list create by current engine, we read from ini file in the install path.");
		ImGui::NewLine();

		ImGui::Indent();
		if (projectPath.empty())
		{
			for (size_t i = 0; i < m_recentProjectList.validId; i++)
			{
				const auto& item = m_recentProjectList.recentOpenProjects[i];
				if (ImGui::Selectable(item.u8().c_str()))
				{
					projectPath = item;
					break;
				}
				ImGui::Spacing();
				ImGui::Spacing();
			}
		}
		ImGui::Unindent();

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing());

		ImGui::Separator();
		ImGui::TextDisabled("Recent open project num: %d.", m_recentProjectList.validId);
	}
	ImGui::End();

	// Project ready, close hub and resizable windows.
	if (!projectPath.empty())
	{
		setupProject(projectPath.u16());
	}
}

bool HubWidget::loadProject()
{
	std::string readPathString;
	nfdchar_t* readPath = NULL;
	nfdresult_t result = NFD_OpenDialog("flower", NULL, &readPath);
	if (result == NFD_OKAY)
	{
		readPathString = readPath;
		free(readPath);
	}
	else if (result == NFD_CANCEL)
	{
		return false;
	}

	std::filesystem::path fp(utf8::utf8to16(readPathString));
	if (!fp.empty() && std::filesystem::exists(fp))
	{
		const auto path = utf8::utf16to8(fp.u16string());
		std::copy(path.begin(), path.end(), m_projectPath);
		m_projectPath[readPathString.size()] = '\0';

		m_bProjectPathReady = true;
		return true;
	}

	return false;
}

bool HubWidget::newProject()
{
	std::string pathStr;
	nfdchar_t* outPathChars = NULL;
	nfdresult_t result = NFD_PickFolder(NULL, &outPathChars);

	if (result == NFD_CANCEL)
	{
		return false;
	}
	else if (result == NFD_OKAY)
	{
		pathStr = outPathChars;
		free(outPathChars);
	}

	if (!pathStr.empty())
	{
		std::filesystem::path fp(utf8::utf8to16(pathStr));
		if (!std::filesystem::is_empty(fp))
		{
			LOG_ERROR("Select new project folder must be an empty directory.")
		}
		else
		{
			std::filesystem::path projectName = fp.filename();
			fp /= (projectName.string() + ".flower");

			pathStr = utf8::utf16to8(fp.u16string());
			std::copy(pathStr.begin(), pathStr.end(), m_projectPath);
			m_projectPath[pathStr.size()] = '\0';

			m_bProjectPathReady = true;
			return true;
		}
	}

	return false;
}

void HubWidget::setupProject(const std::filesystem::path& path)
{
	Project::get().setup(path);
	const auto& projectConfig = Project::get().getPath();

	LOG_TRACE("Start editor with project {}.", projectConfig.projectName.u8());
	LOG_TRACE("Active project path {}, ative root project path {}.", projectConfig.projectFilePath.u8(), projectConfig.rootPath.u8());

	m_recentProjectList.update(path.string());
	Flower::get().onProjectSetup();
}

void RecentOpenProjects::update(const std::filesystem::path& path)
{
	size_t existPos = recentOpenProjects.size() - 1;

	for (size_t i = 0; i < recentOpenProjects.size(); i++)
	{
		if (recentOpenProjects[i].u16() == path)
		{
			existPos = i;
			break;
		}
	}

	// Move back.
	for (size_t i = existPos; i > 0; i--)
	{
		recentOpenProjects[i] = recentOpenProjects[i - 1];
	}

	// Insert first.
	recentOpenProjects[0] = path.u16string();

	updatePathForView();
}

void RecentOpenProjects::updatePathForView()
{
	auto copyArray = recentOpenProjects;
	validId = 0;
	for (size_t i = 0; i < recentOpenProjects.size(); i++)
	{
		if (std::filesystem::exists(recentOpenProjects[i].u16()))
		{
			copyArray[validId] = recentOpenProjects[i];
			validId++;
		}
	}
	recentOpenProjects = copyArray;
}

std::filesystem::path getRecentProjectsPath()
{
	static const std::string kRecentProjectPath = "save/config/flower/recent-projects.ini";
	return kRecentProjectPath;
}

void RecentOpenProjects::save()
{
	updatePathForView();

	const auto savePath = getRecentProjectsPath();
	const auto saveFolder = savePath.parent_path();

	if (!std::filesystem::exists(saveFolder))
	{
		std::filesystem::create_directories(saveFolder);
	}

	std::ofstream os(savePath);
	for (size_t i = 0; i < recentOpenProjects.size(); i++)
	{
		os << recentOpenProjects[i].u8() << std::endl;
	}
}

void RecentOpenProjects::load()
{
	if (std::filesystem::exists(getRecentProjectsPath()))
	{
		std::string u8strEncode;
		std::ifstream is(getRecentProjectsPath());
		for (size_t i = 0; i < recentOpenProjects.size(); i++)
		{
			std::getline(is, u8strEncode);
			recentOpenProjects[i] = u16str(u8strEncode);
		}
	}

	updatePathForView();
}