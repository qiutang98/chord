#include "detail.h"
#include "../flower.h"

#include <ui/ui_helper.h>

using namespace chord::graphics;
using namespace chord;
using namespace chord::ui;

const static std::string ICON_DETAIL = ICON_FA_LIST;

static const std::string DETAIL_SearchIcon = ICON_FA_MAGNIFYING_GLASS;
static const std::string DETAIL_AddIcon = std::string("  ") + ICON_FA_SQUARE_PLUS + std::string("  ADD  ");

WidgetDetail::WidgetDetail(size_t index)
	: IWidget(
		combineIcon("Detail", ICON_DETAIL).c_str(),
		combineIcon(combineIndex("Detail", index), ICON_DETAIL).c_str())
	, m_index(index)
{

}

void WidgetDetail::onInit()
{
	auto& selections = Flower::get().getUISceneContentManager().sceneNodeSelections();

	m_onSelectorChange = selections.onChanged.add([this](Selection<SceneNodeSelctor>& s) 
	{ 
		onOutlinerSelectionChange(s);  
	});
}

void WidgetDetail::onRelease()
{
	auto& selections = Flower::get().getUISceneContentManager().sceneNodeSelections();
	check(selections.onChanged.remove(m_onSelectorChange));
}

void WidgetDetail::onOutlinerSelectionChange(Selection<SceneNodeSelctor>& s)
{
	if (!m_bLock)
	{
		for (auto node : s.getSelections())
		{
			if (node)
			{
				m_inspectNode = node.node;
				return;
			}
		}
	}
}

void WidgetDetail::onTick(const ApplicationTickData& tickData)
{

}


void WidgetDetail::onVisibleTick(const ApplicationTickData& tickData)
{
	ImGui::Spacing();
	auto& selections = Flower::get().getUISceneContentManager().sceneNodeSelections();
	
	if (!m_bLock)
	{
		if (selections.empty())
		{
			ImGui::TextDisabled("No selected node to inspect.");
			return;
		}

		if (selections.num() > 1)
		{
			ImGui::TextDisabled("Multi node detail inspect still no support.");
			return;
		}
	}

	if (m_bLock)
	{
		ImVec2 endPos = ImVec2(
			ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x, 
			ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y);

		ImGui::GetWindowDrawList()->AddRectFilled(
			ImGui::GetCursorScreenPos(), 
			endPos, IM_COL32(125, 10, 20, 25));
	}

	std::shared_ptr<SceneNode> selectedNode = m_inspectNode.lock();
	if (!selectedNode)
	{
		// No valid scene node.
		return;
	}
	ImGui::Spacing(); ImGui::SameLine();
	if (ImGui::Button(m_bLock ? ICON_FA_LOCK : ICON_FA_UNLOCK))
	{
		m_bLock = !m_bLock;
	}
	 ImGui::SameLine();

	// Print detail info.
	ImGui::TextDisabled("Inspecting %s with runtime ID %d.",
		selectedNode->getName().u8().c_str(), selectedNode->getId());

	ImGui::Separator();
	ImGui::Spacing();

	Transform::kComponentUIDrawDetails.onDrawUI(selectedNode->getTransform());
	ImGui::Spacing();

	ui::helpMarker(
		"Scene node state can use for accelerate engine speed.\n"
		"When invisible, renderer can cull this entity before render and save render time\n"
		"But still can simulate or tick logic on entity.\n"
		"When un movable, renderer can do some cache for mesh, skip too much dynamic objects."); ImGui::SameLine();

	const bool bCanSetVisiblity = selectedNode->canSetNewVisibility();
	const bool bCanSetStatic = selectedNode->canSetNewStatic();

	bool bVisibleState = selectedNode->getVisibility();
	bool bMovableState = !selectedNode->getStatic();

	ui::disableLambda([&]()
	{
		if (ImGui::Checkbox("Show", &bVisibleState))
		{
			selectedNode->setVisibility(!selectedNode->getVisibility());
		}
		ui::hoverTip("Scene node visibility state.");
	}, !bCanSetVisiblity);

	ImGui::SameLine();

	ui::disableLambda([&]()
	{
		if (ImGui::Checkbox("Movable", &bMovableState))
		{
			selectedNode->setStatic(!selectedNode->getStatic());
		}
		ui::hoverTip("Entity movable state.");
	}, !bCanSetStatic);

	ImGui::Separator();

	drawComponent(selectedNode);
}

void WidgetDetail::drawComponent(std::shared_ptr<SceneNode> node)
{
	if (ImGui::BeginTable("Add UIC##", 2))
	{
		const float sizeLable = ImGui::GetFontSize();
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, sizeLable * 4.0f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_None);

		ImGui::TableNextColumn();

		if (ImGui::Button((DETAIL_AddIcon).c_str()))
		{
			ImGui::OpenPopup("##XComponentContextMenu_Add");
		}

		if (ImGui::BeginPopup("##XComponentContextMenu_Add"))
		{
			ImGui::TextDisabled("New  Components");
			ImGui::Separator();

			bool bExistOneNewComponent = false;
			

			if (!bExistOneNewComponent)
			{
				ImGui::TextDisabled("Non-Component");
			}
			ImGui::EndPopup();
		}

		ui::hoverTip("Add new component for entity.");

		ImGui::TableNextColumn();
		m_filter.Draw((DETAIL_SearchIcon).c_str());

		ImGui::EndTable();
	}

	ImGui::TextDisabled("Additional components.");

	const ImGuiTreeNodeFlags treeNodeFlags =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_Framed |
		ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_AllowItemOverlap |
		ImGuiTreeNodeFlags_FramePadding;
}