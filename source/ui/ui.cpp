#include <ui/ui.h>
#include <utils/cvar.h>
#include <ui/imgui/imgui.h>
#include <ui/imgui/imgui_impl_glfw.h>
#include <application/application.h>
#include <ui/backend.h>
#include <graphics/graphics.h>
#include <graphics/swapchain.h>
#include <graphics/helper.h>
#include <shader/imgui.hlsl>

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

	static std::string sUIConfigFileSavePath = "config";
	static AutoCVarRef<std::string> cVarUIConfigFileSavePath(
		"r.ui.configPath", 
		sUIConfigFileSavePath, 
		"UI config file path saved path relative application.", 
		EConsoleVarFlags::ReadOnly);

	static std::string sUIFontFilePath = "resource/font/deng.ttf";
	static AutoCVarRef<std::string> cVarsUIFontFilePath(
		"r.ui.font",
		sUIFontFilePath,
		"ImGui font file path.",
		EConsoleVarFlags::ReadOnly);

	static float sUIFontSize = 20.0f;
	static AutoCVarRef<float> cVarsUIFontSize(
		"r.ui.font.size", 
		sUIFontSize, 
		"UI font base size.",
		EConsoleVarFlags::ReadOnly);

	// Setup font for IMGUI windows.
	void UIManager::setupFont()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Load font data to memory.
		ImFont* baseFont = io.Fonts->AddFontFromFileTTF(sUIFontFilePath.c_str(), sUIFontSize, NULL, io.Fonts->GetGlyphRangesChineseFull());
	
		// Upload atlas to GPU.
		{
			using namespace graphics;
			const std::string fonmtTextureName = std::format("{}-UIFontAtlas", Application::get().getName());

			unsigned char* pixels;
			int32 width, height;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

			auto imageCI = helper::buildBasicUploadImageCreateInfo(width, height, VK_FORMAT_R8G8B8A8_UNORM);
			auto uploadVMACI = helper::buildVMAUploadImageAllocationCI();
			m_fontAtlasTexture = std::make_shared<GPUTexture>(fonmtTextureName, imageCI, uploadVMACI);

			SizedBuffer stageSizedBuffer(width * height * 4 * sizeof(char), pixels);
			getContext().syncUploadTexture(*m_fontAtlasTexture, stageSizedBuffer);
		}
	}

	void UIManager::init()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		{
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard controls.
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable gamepad controls.

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

			// Config file store path.
			auto iniFileName = std::format("{0}{1}-ui.ini", sUIConfigFileSavePath, Application::get().getName());
			io.IniFilename = iniFileName.c_str();
		}

		updateStyle();

		// Init glfw backend.
		ImGui_ImplGlfw_InitForVulkan(Application::get().getWindowData().window, true);

		setupFont();

		buildRenderResource();
		{
			auto& swapchain = graphics::getContext().getSwapchain();
			m_afterSwapChainRebuildHandle = swapchain.onAfterSwapchainRecreate.add([this]() { buildRenderResource(); });
		}
	}

	void UIManager::release()
	{
		// Unregister swapchain rebuild functions.
		{
			auto& swapchain = graphics::getContext().getSwapchain();
			check(swapchain.onAfterSwapchainRecreate.remove(m_afterSwapChainRebuildHandle));
		}

		// Free allocated command buffers.
		vkFreeCommandBuffers(
			graphics::getDevice(),
			graphics::getContext().getGraphicsCommandPool().pool(),
			uint32(m_commandBuffers.size()),
			m_commandBuffers.data());
		m_commandBuffers.clear();

		// Release glfw backend.
		ImGui_ImplGlfw_Shutdown();

		ImGui::DestroyContext();
	}

	void UIManager::render()
	{
		ImGui::Render();
	}

	// TODO: Frame marker.
	void UIManager::renderFrame(uint32 backBufferIndex)
	{
		// No meaning for minimized window record command buffer.
		check(!isMainMinimized());

		ImDrawData* mainDrawData = ImGui::GetDrawData();
		{

		}
	}

	void UIManager::updateRemainderWindows()
	{
		ImGuiIO& io = ImGui::GetIO();

		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
	}

	bool UIManager::isMainMinimized()
	{
		ImDrawData* mainDrawData = ImGui::GetDrawData();
		return (mainDrawData->DisplaySize.x <= 0.0f || mainDrawData->DisplaySize.y <= 0.0f);
	}

	void UIManager::newFrame()
	{
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
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

	void UIManager::buildRenderResource()
	{
		auto backbufferCount = graphics::getContext().getSwapchain().getBackbufferCount();
		while (m_commandBuffers.size() < backbufferCount)
		{
			auto cmd = graphics::helper::allocateCommandBuffer(graphics::getContext().getGraphicsCommandPool().pool());
			m_commandBuffers.push_back(cmd);
		}
	}

}

