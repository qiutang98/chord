#include <ui/ui.h>
#include <utils/cvar.h>
#include <ui/imgui/imgui.h>
#include <ui/imgui/imgui_impl_glfw.h>
#include <application/application.h>

namespace chord
{
	static bool bEnableDocking = false;
	static AutoCVarRef<bool> cVarEnableDocking(
		"r.ui.docking",
		bEnableDocking, 
		"Enable imgui docking space or not.", 
		EConsoleVarFlags::ReadOnly);

	static bool bEnableViewports = false;
	static AutoCVarRef<bool> cVarEnableViewports(
		"r.ui.viewports", 
		bEnableViewports, 
		"Enable imgui multi viewports or not.", 
		EConsoleVarFlags::ReadOnly);

	static bool bViewportsNoDecoration = false;
	static AutoCVarRef<bool> cVarViewportsNoDecorated(
		"r.ui.viewports.nodecorated", 
		bViewportsNoDecoration, 
		"Multi viewport no decorated.", 
		EConsoleVarFlags::ReadOnly);

	static std::string sUIConfigFileSavePath = "config/ui.ini";
	static AutoCVarRef<std::string> cVarUIConfigFileSavePath(
		"r.ui.configPath", 
		sUIConfigFileSavePath, 
		"UI config file path saved path relative application.", 
		EConsoleVarFlags::ReadOnly);

	void UIManager::init()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		{
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

			// Configable.
			if (bEnableViewports)
			{
				io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
				io.ConfigViewportsNoDecoration = bViewportsNoDecoration;
			}
			if (bEnableDocking)
			{
				io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			}

			io.IniFilename = sUIConfigFileSavePath.c_str();
		}

		updateStyle();

		// Init glfw backend.
		ImGui_ImplGlfw_InitForVulkan(Application::get().getWindowData().window, true);

		// Init vulkan misc.
		{

		}

	}

	void UIManager::release()
	{
		// Release glfw backend.
		ImGui_ImplGlfw_Shutdown();

		ImGui::DestroyContext();
	}

	void UIManager::updateStyle()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Style setup.
		ImGui::StyleColorsDark();

		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
	}

}

