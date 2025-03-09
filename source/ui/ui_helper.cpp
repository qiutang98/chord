#include <ui/ui_helper.h>
#include <ui/imgui/imgui_internal.h>
#include <asset/asset_common.h>
#include <asset/texture/helper.h>
#include <asset/texture/texture.h>
#include <graphics/graphics.h>

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

	bool ui::drawDVector3(const std::string& label, math::dvec3& values, const math::dvec3& resetValue, float labelWidth)
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

		
				ImGui::DragScalar("##X", ImGuiDataType_Double, &values.x, 0.1f, nullptr, nullptr, "%.2f");
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
				ImGui::DragScalar("##Y", ImGuiDataType_Double, &values.y, 0.1f, nullptr, nullptr, "%.2f");
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
				ImGui::DragScalar("##Z", ImGuiDataType_Double, &values.z, 0.1f, nullptr, nullptr, "%.2f");
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
		if (info.empty())
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

	void ui::drawImage(
		graphics::GPUTextureRef image, 
		const VkImageSubresourceRange& subRange, 
		const ImVec2& size, 
		const ImVec2& uv0, 
		const ImVec2& uv1, 
		const ImVec4& tint_col, 
		const ImVec4& border_col)
	{
		if (ImGui::GetCurrentWindow()->Viewport == nullptr)
		{
			return;
		}

		// Insert pending resource avoid release.
		auto* vrd = (ImGuiViewportData*)ImGui::GetCurrentWindow()->Viewport->RendererUserData;
		if (vrd == nullptr)
		{
			return;
		}

		graphics::Swapchain& swapchain = vrd->swapchain();
		swapchain.addReferenceResource(image);

		// Require image view.
		ImGui::Image(image->requireView(subRange, VK_IMAGE_VIEW_TYPE_2D, true, false).SRV.get(), size, uv0, uv1, tint_col, border_col);
	}

	void ui::drawImage(
		graphics::PoolTextureRef image, 
		const VkImageSubresourceRange& subRange, 
		const ImVec2& size, 
		const ImVec2& uv0, 
		const ImVec2& uv1, 
		const ImVec4& tint_col, 
		const ImVec4& border_col)
	{
		ui::drawImage(image->getGPUTextureRef(), subRange, size, uv0, uv1, tint_col, border_col);
	}

	void chord::ui::drawGroupPannel(
		const char* name,
		std::function<void()>&& lambda, 
		float pushItemWidth, 
		const ImVec2& size)
	{
		ImGui::BeginGroup();

		auto cursorPos = ImGui::GetCursorScreenPos();
		auto itemSpacing = ImGui::GetStyle().ItemSpacing;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

		auto frameHeight = ImGui::GetFrameHeight();
		ImGui::BeginGroup();

		ImVec2 effectiveSize = size;
		effectiveSize.x = size.x < 0.0f ? ImGui::GetContentRegionAvail().x : size.x;

		ImGui::Dummy(ImVec2(effectiveSize.x, 0.0f));
		ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::BeginGroup();
		ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::TextUnformatted(name);
		auto labelMin = ImGui::GetItemRectMin();
		auto labelMax = ImGui::GetItemRectMax();
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::Dummy(ImVec2(0.0, frameHeight + itemSpacing.y));
		ImGui::BeginGroup();

		ImGui::PopStyleVar(2);

		ImGui::GetCurrentWindow()->ContentRegionRect.Max.x -= frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->WorkRect.Max.x -= frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->InnerRect.Max.x -= frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->Size.x -= frameHeight;

		auto itemWidth = ImGui::CalcItemWidth();
		ImGui::PushItemWidth(ImMax(0.0f, itemWidth - frameHeight));

		auto labelRect = ImRect(labelMin, labelMax);

		if (pushItemWidth > 0.0)
		{
			ImGui::PushItemWidth(pushItemWidth);
		}

		lambda();

		if (pushItemWidth > 0.0)
		{
			ImGui::PopItemWidth();
		}

		ImGui::PopItemWidth();

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

		ImGui::EndGroup();
		ImGui::EndGroup();

		ImGui::SameLine(0.0f, 0.0f);
		ImGui::Dummy(ImVec2(frameHeight * 0.5f, 0.0f));
		ImGui::Dummy(ImVec2(0.0, frameHeight - frameHeight * 0.5f - itemSpacing.y));

		ImGui::EndGroup();

		auto itemMin = ImGui::GetItemRectMin();
		auto itemMax = ImGui::GetItemRectMax();

		ImVec2 halfFrame = ImVec2(frameHeight * 0.25f * 0.5f, frameHeight * 0.5f);
		ImRect frameRect = ImRect(
			ImVec2{ itemMin.x + halfFrame.x, itemMin.y + halfFrame.y },
			ImVec2{ itemMax.x - halfFrame.x, itemMax.y });
		labelRect.Min.x -= itemSpacing.x;
		labelRect.Max.x += itemSpacing.x;
		for (int i = 0; i < 4; ++i)
		{
			switch (i)
			{
				// left half-plane
			case 0: ImGui::PushClipRect(ImVec2(-FLT_MAX, -FLT_MAX), ImVec2(labelRect.Min.x, FLT_MAX), true); break;
				// right half-plane
			case 1: ImGui::PushClipRect(ImVec2(labelRect.Max.x, -FLT_MAX), ImVec2(FLT_MAX, FLT_MAX), true); break;
				// top
			case 2: ImGui::PushClipRect(ImVec2(labelRect.Min.x, -FLT_MAX), ImVec2(labelRect.Max.x, labelRect.Min.y), true); break;
				// bottom
			case 3: ImGui::PushClipRect(ImVec2(labelRect.Min.x, labelRect.Max.y), ImVec2(labelRect.Max.x, FLT_MAX), true); break;
			}

			ImGui::GetWindowDrawList()->AddRect(
				frameRect.Min, frameRect.Max,
				ImColor(ImGui::GetStyleColorVec4(ImGuiCol_Border)),
				halfFrame.x);

			ImGui::PopClipRect();
		}

		ImGui::PopStyleVar(2);

		ImGui::GetCurrentWindow()->ContentRegionRect.Max.x += frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->WorkRect.Max.x += frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->InnerRect.Max.x += frameHeight * 0.5f;
		ImGui::GetCurrentWindow()->Size.x += frameHeight;

		ImGui::Dummy(ImVec2(0.0f, 0.0f));

		ImGui::EndGroup();
	}

	void ui::drawImage(
		graphics::GPUTextureAssetRef image,
		const VkImageSubresourceRange& subRange,
		const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col)
	{
		ui::drawImage(image->getReadyImage(), subRange, size, uv0, uv1, tint_col, border_col);
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


