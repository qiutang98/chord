#include <ui/ui_helper.h>
#include <ui/imgui/imgui_internal.h>

namespace chord::ui
{
	void ui::disableLambda(std::function<void()>&& lambda, bool bDisable)
	{
		if (bDisable)
		{
			ImGui::BeginDisabled();
		}

		lambda();

		if (bDisable)
		{
			ImGui::EndDisabled();
		}
	}

	bool ui::treeNodeEx(const char* idLabel, const char* showlabel, ImGuiTreeNodeFlags flags)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		return ImGui::TreeNodeBehavior(window->GetID(idLabel), flags, showlabel, NULL);
	}

	void ui::hoverTip(const char* desc)
	{
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	ImGuiPopupSelfManagedOpenState::ImGuiPopupSelfManagedOpenState(
		const std::string& titleName,
		ImGuiWindowFlags flags)
		: m_flags(flags)
		, m_popupName(titleName)
	{

	}

	void ImGuiPopupSelfManagedOpenState::draw()
	{
		if (m_bShouldOpenPopup)
		{
			ImGui::OpenPopup(m_popupName.c_str());
			m_bShouldOpenPopup = false;
		}

		bool state = m_bPopupOpenState;
		if (ImGui::BeginPopupModal(m_popupName.c_str(), &m_bPopupOpenState, m_flags))
		{
			ImGui::PushID(m_uuid.c_str());

			onDraw();

			ImGui::PopID();
			ImGui::EndPopup();
		}

		if (state != m_bPopupOpenState)
		{
			onClosed();
		}
	}

	bool ImGuiPopupSelfManagedOpenState::open()
	{
		if (m_bShouldOpenPopup)
		{
			return false;
		}

		m_bShouldOpenPopup = true;
		m_bPopupOpenState  = true;

		return true;
	}
}


