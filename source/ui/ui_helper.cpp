#include <ui/ui_helper.h>
#include <ui/imgui/imgui_internal.h>
#include <asset/asset_common.h>

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

	bool drawVector3(const std::string& label, math::vec3& values, const math::vec3& resetValue, float labelWidth)
	{
		constexpr const char* kResetIcon = ICON_FA_REPLY;
		const auto srcData = values;

		ImGuiIO& io = ImGui::GetIO();
		ImGui::PushID(label.c_str());

		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonSize = ImVec2(lineHeight, lineHeight);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f });
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{ 0.0f, 0.0f });
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{ 1.0f, 1.0f, 1.0f, 0.04f });
		if (ImGui::BeginTable("Vec3UI", 5, ImGuiTableFlags_Borders))
		{
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_None);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_None);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_None);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(kResetIcon).x + ImGui::GetStyle().FramePadding.x * 2.0f);

			ImGui::TableNextColumn(); // label

			// Center label.
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + GImGui->Style.FramePadding.y);
			ImGui::Text(label.c_str());


			ImGui::TableNextColumn();
			// X
			{
				
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.6f, 0.2f, 0.1f, 0.15f });
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.6f, 0.2f, 0.1f, 1.0f });
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.9f, 0.9f, 0.9f, 1.0f });
				if (ImGui::Button("X", buttonSize))
				{
					values.x = resetValue.x;
				}

				ImGui::PopStyleColor(3);

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
				ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
				ImGui::PopItemWidth();

			}

			ImGui::TableNextColumn();
			// Y
			{
				
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.6f, 0.2f, 0.15f });
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.6f, 0.2f, 1.0f });
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.9f, 0.9f, 0.9f, 1.0f });
				if (ImGui::Button("Y", buttonSize))
				{
					values.y = resetValue.y;
				}

				ImGui::PopStyleColor(3);

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
				ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
				ImGui::PopItemWidth();
				ImGui::SameLine();
			}
			ImGui::TableNextColumn();
			// Z
			{
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.5f, 0.6f, 0.15f });
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.5f, 0.8f, 1.0f });
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.9f, 0.9f, 0.9f, 1.0f });
				if (ImGui::Button("Z", buttonSize))
				{
					values.z = resetValue.z;
				}

				ImGui::PopStyleColor(3);

				ImGui::SameLine();
				ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
				ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
				ImGui::PopItemWidth();
				ImGui::SameLine();
			}

			ImGui::TableNextColumn();
			// Reset.
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 1.0f, 1.0f, 1.0f, 0.075f });
			if (ImGui::Button(kResetIcon))
			{
				values.x = resetValue.x;
				values.y = resetValue.y;
				values.z = resetValue.z;
			}
			ImGui::PopStyleColor();

			ImGui::EndTable();
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
		ImGui::PopID();

		return srcData != values;
	}

	void ui::helpMarker(const char* desc)
	{
		ImGui::TextDisabled(" (?) ");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	bool ui::buttonCenteredOnLine(const char* label, math::vec2 widgetSize, float alignment)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		float size = widgetSize.x;
		float avail = ImGui::GetContentRegionAvail().x;

		float off = (avail - size) * alignment;
		if (off > 0.0f)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

		return ImGui::Button(label, widgetSize);
	}

	void inspectAssetSaveInfo(const AssetSaveInfo& info)
	{
		if (!info.empty())
		{
			ImGui::TextDisabled("Empty asset save info!");
		}
		else if (info.isTemp())
		{
			ImGui::TextDisabled("Temp: {}", info.getName().u8().c_str());
		}
		else
		{
			ImGui::TextDisabled(utf8::utf16to8(info.relativeAssetStorePath().u16string()).c_str());
		}

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


