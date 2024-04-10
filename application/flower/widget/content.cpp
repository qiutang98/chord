#include "content.h"
#include "../flower.h"
#include <project.h>
#include <ui/ui_helper.h>

using namespace chord::graphics;
using namespace chord;
using namespace chord::ui;

static const std::string kIconContentImport  = fontIcon::fileIn  + utf8::utf16to8(u" 导入");
static const std::string kIconContentNew     = fontIcon::newFile + utf8::utf16to8(u" 新建");
static const std::string kIconContentSave    = fontIcon::save    + utf8::utf16to8(u" 保存");
static const std::string kIconContentSaveAll = fontIcon::saveall + utf8::utf16to8(u" 全部保存");
static constexpr const char* kIconContentSearch  = fontIcon::magnifying;
static constexpr const char* kIconContentTitle   = fontIcon::folder2;

static constexpr const char* kRightClickedMenuName = "##RightClickedMenu";

static const float kMinSnapShotIconSize = 2.0f;
static const float kMaxSnapShotIconSize = 8.0f;

WidgetContent::WidgetContent(size_t index)
	: IWidget(
		combineIcon("Content", kIconContentTitle).c_str(),
		combineIcon(combineIndex("Content", index), kIconContentTitle).c_str())
	, m_index(index)
{
	m_snapshotItemIconSize = 4.0f;
}

void WidgetContent::onInit()
{
	m_bShow = false;

	// Setup project first.
	setupProject();

	m_onTreeUpdateHandle = Flower::get().getContentManager().onTreeUpdate.add([this](const ProjectContentEntryTree& tree)
	{
		this->onTreeUpdate(tree);
	});
}

void WidgetContent::onRelease()
{
	check(Flower::get().getContentManager().onTreeUpdate.remove(m_onTreeUpdateHandle));
}

void WidgetContent::onTreeUpdate(const ProjectContentEntryTree& tree)
{
	m_selections.clear();
	m_treeviewHoverEntry.reset();
	m_openedEntry.clear();
}

void WidgetContent::setupProject()
{
	check(Project::get().isSetup());

	// Default set active folder entry to root.
	m_activeFolder = Flower::get().getContentManager().getTree().getRoot();
}

void WidgetContent::setActiveEntry(ProjectContentEntryRef entry)
{
	// Use closest folder as active folder.
	if (entry->isFoleder())
	{
		m_activeFolder = entry;
	}
	else
	{
		m_activeFolder = entry->getParent();
	}

	// Update selections in model.
	m_selections.clear();
	m_selections.add(entry);
}



void WidgetContent::onTick(const chord::ApplicationTickData& tickData)
{

}

void WidgetContent::onVisibleTick(const chord::ApplicationTickData& tickData)
{
	ImGui::Separator();
	{
		drawMenu(tickData);
	}

	ImGui::Separator();
	const auto& projectConfig = Project::get().getPath();
	{
		ImGui::TextDisabled("Working project: %s and working path: %s.", projectConfig.projectName.u8().c_str(), projectConfig.rootPath.u8().c_str());
		ImGui::SameLine();

		std::string activeFolderName = utf8::utf16to8(m_activeFolder.lock()->getPath());
		ImGui::Text("Inspecting folder path: %s.", activeFolderName.c_str());
	}

	drawContent(tickData);
}

/////////////////////////////////////////////////////////////////////////////////////
// Draw functions.

void WidgetContent::drawContent(const chord::ApplicationTickData& tickData)
{
	const float footerHeightToReserve = ImGui::GetTextLineHeightWithSpacing() * 1.25f;
	const float sizeLable = ImGui::GetFontSize();

	// Reset tree view hovering entry.
	m_treeviewHoverEntry.reset();

	auto workingEntry = m_activeFolder.lock();
	auto root = Flower::get().getContentManager().getTree().getRoot();

	if (ImGui::BeginTable("AssetContentTable", 2, ImGuiTableFlags_BordersInner | ImGuiTableFlags_Resizable))
	{
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, sizeLable * 14.0f);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_None);

		// Row #0 is tree view.
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		{
			ImGui::PushID("##ContentViewItemTreeView");
			ImGui::BeginChild("ScrollingRegion", ImVec2(0, ImGui::GetContentRegionAvail().y - footerHeightToReserve), false, ImGuiWindowFlags_HorizontalScrollbar);
			{
				drawContentTreeView(root);
			}
			ImGui::EndChild();
			if (ImGui::IsItemClicked() && (!m_treeviewHoverEntry.lock()) && (!ImGui::GetIO().KeyCtrl))
			{
				setActiveEntry(root);
			}
			if (ImGui::IsItemClicked(1) && !getSelections().empty())
			{
				ImGui::OpenPopup(kRightClickedMenuName);
			}
			if (ImGui::BeginPopup(kRightClickedMenuName))
			{
				drawRightClickedMenu();
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		// Row #1 is content snap shot.
		ImGui::TableSetColumnIndex(1);
		{
			ImGui::PushID("##ContentViewItemInspector");
			ImGui::BeginChild("ScrollingRegion2", ImVec2(0, -footerHeightToReserve), false);
			if (workingEntry)
			{
				drawContentSnapShot(workingEntry);
			}
			ImGui::EndChild();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Separator();
	size_t itemNum = 0;
	if (workingEntry)
	{
		itemNum = workingEntry->getChildren().size();
	}
	ImGui::Text("  %d  items.", itemNum);
}

void WidgetContent::drawMenu(const chord::ApplicationTickData& tickData)
{
	const float sizeLable = ImGui::GetFontSize();
	if (ImGui::BeginTable("Import UIC##", 5))
	{
		static const float padSize = ImGui::GetItemSpacing();

		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, padSize + ImGui::CalcTextSize(kIconContentImport.c_str()).x);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, padSize + ImGui::CalcTextSize(kIconContentNew.c_str()).x);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, padSize + ImGui::CalcTextSize(kIconContentSave.c_str()).x);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, padSize + ImGui::CalcTextSize(kIconContentSaveAll.c_str()).x);
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_None);


		ImGui::TableNextColumn();

		static const char* kImport = "##XAssetMenu_Import";
		static const char* kCreate = "##XAssetMenu_Create";


		if (ImGui::Button((kIconContentImport).c_str()))
		{
			ImGui::OpenPopup(kImport);
		}
		hoverTip("Import new asset from disk.");

		if (ImGui::BeginPopup(kImport))
		{
			drawAssetImport();

			ImGui::EndPopup();
		}

		ImGui::TableNextColumn();

		if (ImGui::Button((kIconContentNew).c_str()))
		{
			ImGui::OpenPopup(kCreate);

		}
		hoverTip("Create new asset.");

		if (ImGui::BeginPopup(kCreate))
		{
			ImGui::TextDisabled("Create  Assets:");
			ImGui::Separator();

			// TODO:
			// drawAssetCreate();

			ImGui::EndPopup();
		}

		ImGui::TableNextColumn();

		if (ImGui::Button((kIconContentSave).c_str()))
		{
			// TODO:
			/*
			const auto& assets = m_editor->getAssetSelected();
			for (const auto& assetPath : assets)
			{
				auto pathCopy = assetPath;
				const auto relativePath = buildRelativePathUtf8(m_editor->getProjectRootPathUtf16(), pathCopy.replace_extension());
				getAssetSystem()->getAssetByRelativeMap(relativePath)->saveAction();
			}
			*/
		}
		hoverTip("Save select asset.");
		ImGui::TableNextColumn();

		if (ImGui::Button((kIconContentSaveAll).c_str()))
		{

		}
		hoverTip("Save all assets.");
		ImGui::TableNextColumn();

		m_filter.Draw(kIconContentSearch);
		ImGui::EndTable();
	}
}

void WidgetContent::drawContentTreeView(ProjectContentEntryRef entry)
{
	// Should we draw with tree node.
	const bool bTreeNode = entry->isFoleder() && (!entry->isChildrenEmpty());

	// Get node flags.
	ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanFullWidth;
	nodeFlags |= bTreeNode ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf;

	const ProjectContentEntrySelect assetSelector(entry);
	if (getSelections().isSelected(assetSelector))
	{
		nodeFlags |= ImGuiTreeNodeFlags_Selected;
	}

	// Add icon decorate.
	u8str showName = entry->getName();
	constexpr const char* padStr = "  ";
	if (entry->isFoleder())
	{
		if (entry->isChildrenEmpty())
		{
			showName = std::string(fontIcon::folder3) + padStr + showName;
		}
		else
		{
			if (m_openedEntry.contains(entry->getHashId()))
			{
				showName = std::string(fontIcon::folderopen) + padStr + showName; // Open.
			}
			else
			{
				showName = std::string(fontIcon::folder4) + padStr + showName;
			}
		}
	}
	else
	{
		showName = std::string(fontIcon::file2) + padStr + showName;
	}

	// Draw tree node.
	bool bNodeOpen = treeNodeEx(entry->getName().c_str(), showName.c_str(), nodeFlags);
	if (bNodeOpen)
	{
		m_openedEntry.insert(entry->getHashId());
	}
	else
	{
		m_openedEntry.erase(entry->getHashId());
	}

	// Action tick.
	if (ImGui::IsItemClicked(0))
	{
		if (ImGui::GetIO().KeyCtrl)
		{
			if (getSelections().isSelected(assetSelector))
			{
				getSelections().remove(assetSelector);
			}
			else
			{
				getSelections().add(assetSelector);
			}
		}
		else
		{
			// Switch active entry.
			setActiveEntry(entry);
		}
	}

	const bool bItemHover = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
	if (bItemHover)
	{
		m_treeviewHoverEntry = entry;
	}

	// Recursive draw.
	if (bNodeOpen)
	{
		if (bTreeNode)
		{
			for (const auto& child : entry->getChildren())
			{
				drawContentTreeView(child);
			}
		}
		ImGui::TreePop();
	}
}

void WidgetContent::drawRightClickedMenu()
{
	check(!getSelections().empty());

	ImGui::TextDisabled("Assets Menu");
	ImGui::Separator();

	static const std::string kNewItemName    = fontIcon::newFile + utf8::utf16to8(u"  新建 ...");
	static const std::string kImportItemName = fontIcon::fileIn  + utf8::utf16to8(u"  导入 ...");
	static const std::string kCopyItemName   = fontIcon::clone   + utf8::utf16to8(u"  复制");
	static const std::string kPasteItemName  = fontIcon::paste + utf8::utf16to8(u"  粘贴");
	static const std::string kDeleteItemName = fontIcon::trashCan + utf8::utf16to8(u"  删除");

	const bool bSingleSelected = getSelections().num() == 1;
	const bool bSingleFolderSelected = getSelections().getSelections().at(0).entry.lock()->isFoleder();

	if (bSingleSelected)
	{
		if (bSingleFolderSelected)
		{
			if (ImGui::BeginMenu(kNewItemName.c_str()))
			{


				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(kImportItemName.c_str()))
			{
				drawAssetImport();
				ImGui::EndMenu();
			}
		}
	}

	if (ImGui::Selectable(kDeleteItemName.c_str()))
	{


	}

	ImGui::Separator();
	if (ImGui::Selectable(kCopyItemName.c_str()))
	{

	}
	if (ImGui::Selectable(kPasteItemName.c_str()))
	{

	}
}

void WidgetContent::drawAssetImport()
{
	// TODO:
}

void WidgetContent::drawItemSnapshot(float drawDimSize, ProjectContentEntryRef entry)
{
	const ProjectContentEntrySelect entrySelector(entry);

	static std::hash<uint64_t> haser;
	float textH = ImGui::GetTextLineHeightWithSpacing();

	const bool bItemSeleted = getSelections().isSelected(entrySelector);

	const math::vec2 startPos = ImGui::GetCursorScreenPos();
	const math::vec2 endPos = startPos + drawDimSize;

	ImGui::PushID(int(haser(entry->getHashId())));
	ImGui::BeginChild(
		entry->getName().c_str(),
		{ drawDimSize ,  drawDimSize + textH * 3.0f },
		false,
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoScrollbar);

	bool bItemHover = ImGui::IsMouseHoveringRect(startPos, endPos);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
	{
		if (bItemHover)
		{
			// Double clicked
			if (ImGui::IsMouseDoubleClicked(0))
			{
				if (entry->isFoleder())
				{
					setActiveEntry(entry);
				}
				else
				{
					auto copyPath = entry->getPath();
					// TODO:
				}
			}

			// Single click
			if (ImGui::IsMouseClicked(0))
			{
				if (ImGui::GetIO().KeyCtrl)
				{
					if (getSelections().isSelected(entrySelector))
					{
						getSelections().remove(entrySelector);
					}
					else
					{
						getSelections().add(entrySelector);
					}
				}
				else
				{
					getSelections().clear();
					getSelections().add(entrySelector);
				}
			}
		}

		if (bItemSeleted && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			auto& dragDropAssets = Flower::get().getContentManager().getDragDropAssets();

			dragDropAssets.selectAssets.clear();
			for (const auto& selector : getSelections().getSelections())
			{
				if (auto entry = selector.entry.lock())
				{
					dragDropAssets.selectAssets.insert(entry->getPath());
				}
			}

			ImGui::SetDragDropPayload(
				ProjectContentManager::getDragDropAssetsName(),
				(void*)&dragDropAssets,
				sizeof(void*));

			const auto& tree = Flower::get().getContentManager().getTree();
			for (const auto& id : dragDropAssets.selectAssets)
			{
				std::string showName = utf8::utf16to8(id.u16string());
				ImGui::Text(showName.c_str());
			}

			ImGui::EndDragDropSource();
		}

		// Decorated fill
		{
			ImGui::GetWindowDrawList()->AddRectFilled(
				startPos,
				ImVec2(startPos.x + drawDimSize, startPos.y + drawDimSize + textH * 3.0f),
				bItemSeleted ? IM_COL32(88, 150, 250, 81) : IM_COL32(51, 51, 51, 190));
		}

		ImVec2 uv0{ 0.0f, 0.0f };
		ImVec2 uv1{ 1.0f, 1.0f };

		auto set = entry->getSet(uv0, uv1);
		ImGui::Image(set, { drawDimSize , drawDimSize }, uv0, uv1);


		const float indentSize = ImGui::GetFontSize() * 0.25f;
		ImGui::Indent(indentSize);
		{
			ImGui::Spacing();
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + drawDimSize - indentSize);
			ImGui::Text(entry->getName().c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::Unindent();

	}
	ImGui::PopStyleVar();
	ImGui::EndChild();

	ImGui::GetWindowDrawList()->AddRect(
		startPos,
		endPos,
		bItemHover ? IM_COL32(250, 244, 11, 255) : IM_COL32(255, 255, 255, 80), bItemHover ? 1.0f : 0.0f, 0, bItemHover ? 1.5f : 1.0f);

	ImGui::GetWindowDrawList()->AddRect(
		math::vec2(startPos.x, startPos.y + drawDimSize),
		ImGui::GetItemRectMax(),
		IM_COL32(255, 255, 255, 39));

	ui::hoverTip(entry->getName().c_str());
	ImGui::PopID();
}

void WidgetContent::drawContentSnapShot(ProjectContentEntryRef workingEntry)
{
	const auto& children = workingEntry->getChildren();
	const auto inspectItemNum = children.size();

	const auto availRegion = ImGui::GetContentRegionAvail();
	const float itemDimSize = ImGui::GetTextLineHeightWithSpacing() * m_snapshotItemIconSize;

	const float kPadFirstColumDimX = ImGui::GetItemSpacing(); // ImGui::GetItemSpacing();
	const uint32_t drawItemPerRow = uint32_t(math::max(1.0f, (availRegion.x - kPadFirstColumDimX - ImGui::GetTextLineHeightWithSpacing() * 2.0f) / (itemDimSize + ImGui::GetStyle().ItemSpacing.x)));

	const size_t minDrawRowNum = size_t(math::max(1.0f, math::ceil(availRegion.y / itemDimSize)));
	const uint32_t drawRowNum = uint32_t(math::max(minDrawRowNum, inspectItemNum / drawItemPerRow + 1));
	if (ImGui::BeginTable("##table_scrolly_snapshot", drawItemPerRow + 1,
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Hideable |
		ImGuiTableFlags_NoClip |
		ImGuiTableFlags_NoBordersInBody, availRegion))
	{
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, kPadFirstColumDimX);
		for (size_t i = 1; i < drawItemPerRow + 1; i++)
		{
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, itemDimSize);
		}

		ImGuiListClipper clipper;
		clipper.Begin(int(drawRowNum));
		while (clipper.Step())
		{
			for (size_t row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
			{
				ImGui::TableNextRow();
				ImGui::PushID(int(row));

				for (size_t colum = 1; colum < drawItemPerRow + 1; colum++)
				{
					size_t drawId = row * drawItemPerRow + (colum - 1);
					if (drawId < inspectItemNum)
					{
						ImGui::TableSetColumnIndex(int(colum));
						drawItemSnapshot(itemDimSize, children.at(drawId));
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
	}

	if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
	{
		m_snapshotItemIconSize += ImGui::GetIO().MouseWheel;
	}
	
	m_snapshotItemIconSize = math::clamp(m_snapshotItemIconSize, kMinSnapShotIconSize, kMaxSnapShotIconSize);

	if (ImGui::IsMouseClicked(1) &&
		ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) &&
		!getSelections().empty())
	{
		ImGui::OpenPopup(kRightClickedMenuName);
	}
	if (ImGui::BeginPopup(kRightClickedMenuName))
	{
		drawRightClickedMenu();
		ImGui::EndPopup();
	}
}