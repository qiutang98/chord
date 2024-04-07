#pragma warning(disable: 4996)

#include "downbar.h"

#include <ui/imgui/imgui_internal.h>
#include <ui/ui_helper.h>

using namespace chord;
using namespace chord::graphics;

DownbarWidget::DownbarWidget() : IWidget("##Downbar", "##Downbar")
{
	// Downbar don't need to show widget.
	m_bShow = false;
}

using TimePoint = std::chrono::system_clock::time_point;
static inline std::string downbarSerializeTimePoint(const TimePoint& time, const std::string& format)
{
	std::time_t tt = std::chrono::system_clock::to_time_t(time);
	std::tm tm = *std::localtime(&tt);
	std::stringstream ss;
	ss << std::put_time(&tm, format.c_str());
	return ss.str();
}

static inline bool beginDownBar(float heightScale, const char* name)
{
	ImGuiContext& g = *GImGui;
	ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetWindowViewport();

	g.NextWindowData.MenuBarOffsetMinVal = ImVec2(g.Style.DisplaySafeAreaPadding.x, ImMax(g.Style.DisplaySafeAreaPadding.y - g.Style.FramePadding.y, 0.0f));
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
	float height = ImGui::GetFrameHeight() * heightScale;
	bool bOpen = ImGui::BeginViewportSideBar(name, viewport, ImGuiDir_Down, height, windowFlags);
	g.NextWindowData.MenuBarOffsetMinVal = ImVec2(0.0f, 0.0f);

	if (bOpen)
	{
		ImGui::BeginMenuBar();
	}	
	else
	{
		ImGui::End();
	}
	return bOpen;
}

static inline void endDownBar()
{
	ImGui::EndMenuBar();

	ImGuiContext& g = *GImGui;
	if (g.CurrentWindow == g.NavWindow && g.NavLayer == ImGuiNavLayer_Main && !g.NavAnyRequest)
	{
		ImGui::FocusTopMostWindowUnderOne(g.NavWindow, NULL);
	}
		
	ImGui::End();
}

void DownbarWidget::onTick(const chord::ApplicationTickData& tickData)
{
	bool bHide = true;

	static ImGuiWindowFlags flag =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoMouseInputs |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavInputs |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_UnsavedDocument |
		ImGuiWindowFlags_NoTitleBar;

	// Current default use main viewport.
	ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);

	if (!ImGui::Begin(getName().c_str(), &bHide, flag))
	{
		ImGui::End();
		return;
	}


	std::string fpsText;
	float fps;
	{
		fps = math::clamp(float(tickData.fpsUpdatedPerSecond), 0.0f, 1000.0f);
		std::stringstream ss;
		ss << std::setw(4) << std::left << std::setfill(' ') << std::fixed << std::setprecision(0) << fps;

		static const std::string name = utf8::utf16to8(u"帧率 ");
		fpsText = name + ss.str();
	}

	ImGui::PushStyleColor(ImGuiCol_FrameBg,      ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
	ImGui::PushStyleColor(ImGuiCol_Border,       ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
	ImGui::PushStyleColor(ImGuiCol_BorderShadow, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
	if (beginDownBar(0.98f, std::format("##ViewportName: {}", getName()).c_str()))
	{
		// draw engine info
		{
			TimePoint input = std::chrono::system_clock::now();
			std::string name = downbarSerializeTimePoint(input, "%Y/%m/%d %H:%M:%S");

			static const std::string sDevName = "Flower-Alpha-Ver0.0.0";
			ImGui::Text(sDevName.c_str());
			ImGui::Text(name.c_str());
		}

		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float textStartPositionX = ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(fpsText.c_str()).x - ImGui::GetScrollX() - 1 * ImGui::GetStyle().ItemSpacing.x;

		constexpr math::vec4 goodColor = { 0.1f, 0.9f, 0.05f, 1.0f };
		constexpr math::vec4 badColor  = { 0.9f, 0.1f, 0.10f, 1.0f };

		{
			// prepare fps color state
			constexpr float lerpMax = 60.0f;
			constexpr float lerpMin = 10.0f;

			const float lerpFps = (math::clamp(fps, lerpMin, lerpMax) - lerpMin) / (lerpMax - lerpMin);
			math::vec4 lerpColorfps = math::lerp(badColor, goodColor, lerpFps);

			// draw fps state.
			{
				ImGui::PushStyleColor(ImGuiCol_Text, lerpColorfps);
				ImGui::SetCursorPosX(textStartPositionX);
				ImGui::Text("%s", fpsText.c_str());
				ImGui::PopStyleColor();
			}
		}

		ImGui::PopStyleColor(3);
		endDownBar();
	}

	ImGui::End();
}