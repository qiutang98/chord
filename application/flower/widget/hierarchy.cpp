#include "hierarchy.h"
#include <ui/ui_helper.h>
#include "../flower.h"
#include "../manager/project_content.h"

using namespace chord;
using namespace chord::ui;

const char* WidgetHierarchy::DrawContext::kHierarchyPopupMenuName = "##HierarchyContextMenu";
const char* WidgetHierarchy::DrawContext::kHierarchyDragDropName  = "##HierarchyDragDropName";

static constexpr const char* kIconHierarchyTitle = ICON_FA_CHESS_QUEEN;

WidgetHierarchy::WidgetHierarchy()
	: IWidget(
		combineIcon("Hierarchy", kIconHierarchyTitle).c_str(),
		combineIcon("Hierarchy", kIconHierarchyTitle).c_str())
{
	m_worldManager   = &Application::get().getEngine().getSubsystem<WorldSubSystem>();
	m_worldManagerUI = &Flower::get().getUIWorldContentManager();
}

void WidgetHierarchy::onInit()
{

}

void WidgetHierarchy::onRelease()
{

}

void WidgetHierarchy::onTick(const ApplicationTickData& tickData)
{

}

void WidgetHierarchy::onVisibleTick(const ApplicationTickData& tickData)
{
	auto activeWorld = m_worldManager->getActiveWorld();

	// Reset draw index.
	m_drawContext.drawIndex = 0;
	m_drawContext.hoverEntity = entt::null;

	// Header text.
	ImGui::Spacing();
	ImGui::TextDisabled("%s  Active world:  %s.", ICON_FA_FAN, activeWorld->getName().u8().c_str());
	ImGui::Spacing();
	ImGui::Separator();

	// 
	const float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("HierarchyScrollingRegion", ImVec2(0, -footerHeightToReserve), true, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, m_drawContext.scenePaddingItemY));
	{
		entt::entity root = activeWorld->getRoot();
		entt::registry& registry = activeWorld->getRegistry();

		const auto& relationship = registry.get<ecs::RelationshipComponent>(root);
		entt::entity iterE = relationship.first;
		for (auto i = 0; i < relationship.childrenCount; i++)
		{
			const auto& relationshipIter = registry.get<ecs::RelationshipComponent>(iterE);
			check(iterE != entt::null);
			drawEntity(iterE);
			iterE = relationshipIter.next;
		}


		handleEvent();
		popupMenu();
	}
	ImGui::PopStyleVar(1);
	ImGui::EndChild();

	acceptDragdrop(true);

	// End decorated text.
	ImGui::Separator(); ImGui::Spacing();
	ImGui::Text("  %d scene nodes.", activeWorld->getEntityCount() - 1); // -1 for root entity.

	m_drawContext.expandNodeInTreeView.clear();
}

void WidgetHierarchy::drawEntity(entt::entity entity)
{
	auto activeWorld = m_worldManager->getActiveWorld();
	entt::registry& registry = activeWorld->getRegistry();

	const auto& relationship = registry.get<ecs::RelationshipComponent>(entity);

	// This is an event draw or not.
	const bool bEvenDrawIndex = m_drawContext.drawIndex % 2 == 0;
	m_drawContext.drawIndex++;

	// This is a tree node or not.
	const bool bTreeNode = relationship.childrenCount > 0;

	const bool bVisibilityNodePrev = registry.view<ecs::VisibleTag>().contains(entity); 
	const bool bStaticNodePrev = registry.view<ecs::StaticTag>().contains(entity);

	bool bEditingName = false;
	bool bPushEditNameDisable = false;
	bool bNodeOpen;
	bool bSelectedNode = false;

	auto& selections = m_worldManagerUI->worldEntitySelections();

	// Visible and static style prepare.
	if (!bVisibilityNodePrev) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.39f);
	if (!bStaticNodePrev) ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor::HSV(0.15f, 0.6f, 1.0f));
	{
		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanFullWidth;
		nodeFlags |= bTreeNode ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf;

		const bool bThisNodeSelected = selections.isSelected(WorldEntitySelctor(entity));

		if (bThisNodeSelected)
		{
			nodeFlags |= ImGuiTreeNodeFlags_Selected;
		}

		auto& nodeNameUtf8 = registry.get<ecs::NameComponent>(entity);

		if (m_drawContext.bRenameing && bThisNodeSelected)
		{
			bNodeOpen = true;
			bEditingName = true;

			bPushEditNameDisable = true;
			ImGui::Indent();

			ImGui::Text("  %s  ", ICON_FA_ELLIPSIS);
			ImGui::SameLine();

			strcpy_s(m_drawContext.inputBuffer, nodeNameUtf8.name.u8().c_str());
			if (ImGui::InputText(" ", m_drawContext.inputBuffer, IM_ARRAYSIZE(m_drawContext.inputBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				std::string newName = m_drawContext.inputBuffer;
				if (!newName.empty())
				{
					// MODIFY.
					nodeNameUtf8.name = m_worldManagerUI->addUniqueIdForName(u16str(newName));
				}
				m_drawContext.bRenameing = false;
			}

			if (!ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				m_drawContext.bRenameing = false;
			}
		}
		else
		{
			if (m_drawContext.expandNodeInTreeView.contains(entity))
			{
				ImGui::SetNextItemOpen(true);
			}

			bNodeOpen = ImGui::TreeNodeEx(
				reinterpret_cast<void*>(static_cast<intptr_t>(entity)),
				nodeFlags,
				" %s    %s", bTreeNode ? ICON_FA_FOLDER : ICON_FA_FAN,
				nodeNameUtf8.name.u8().c_str());
		}

		// update hover node.
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
		{
			m_drawContext.hoverEntity = entity;
		}

		if (ImGui::IsItemClicked())
		{
			if (ImGui::GetIO().KeyCtrl)
			{
				if (selections.isSelected(WorldEntitySelctor(m_drawContext.hoverEntity)))
				{
					selections.remove(WorldEntitySelctor(m_drawContext.hoverEntity));
				}
				else
				{
					selections.add(WorldEntitySelctor(m_drawContext.hoverEntity));
				}
			}
			else
			{
				selections.clear();
				selections.add(WorldEntitySelctor(m_drawContext.hoverEntity));
			}
		}

		// Start drag drop.
		beginDragDrop(entity);
		acceptDragdrop(false);

		auto colorBg = bEvenDrawIndex ?
			ImGui::GetStyleColorVec4(ImGuiCol_TableRowBg) :
			ImGui::GetStyleColorVec4(ImGuiCol_TableRowBgAlt);

		auto itemEndPosX = ImGui::GetCursorPosX();
		itemEndPosX += ImGui::GetItemRectSize().x;

		ImGui::GetWindowDrawList()->AddRectFilled(
			{ ImGui::GetItemRectMin().x - ImGui::GetStyle().ItemSpacing.x * 0.5f, ImGui::GetItemRectMin().y - 0.5f * m_drawContext.scenePaddingItemY },
			{ ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x * 0.5f, ImGui::GetItemRectMax().y + 0.5f * m_drawContext.scenePaddingItemY },
			IM_COL32(colorBg.x * 255, colorBg.y * 255, colorBg.z * 255, colorBg.w * 255));

		if (!bEditingName)
		{
			ImGui::SameLine();
			handleDrawState(entity);
		}
	}

	// Visible and static style pop.
	if (!bVisibilityNodePrev) ImGui::PopStyleVar();
	if (!bStaticNodePrev) ImGui::PopStyleColor();

	if (bNodeOpen)
	{
		if (bTreeNode)
		{
			entt::entity iterE = relationship.first;
			for (auto i = 0; i < relationship.childrenCount; i++)
			{
				const auto& relationshipIter = registry.get<ecs::RelationshipComponent>(iterE);
				check(iterE != entt::null);
				drawEntity(iterE);
				iterE = relationshipIter.next;
			}
		}

		if (!bEditingName)
		{
			ImGui::TreePop();
		}
	}

	if (bPushEditNameDisable)
	{
		ImGui::Unindent();
	}
}

void WidgetHierarchy::handleEvent()
{
	const auto bWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if (!bWindowHovered)
	{
		return;
	}

	auto& selections = m_worldManagerUI->worldEntitySelections();

	// Prepare seleted one node state.
	entt::entity selectedOneEntity = entt::null;
	if (selections.num() == 1)
	{
		selectedOneEntity = selections.getSelections()[0].entity;
	}

	const auto bLeftClick = ImGui::IsMouseClicked(0);
	const auto bRightClick = ImGui::IsMouseClicked(1);
	const auto bDoubleClick = ImGui::IsMouseDoubleClicked(0);

	// Click empty state.
	if (!ImGui::GetIO().KeyCtrl)
	{
		// Update selected node to root if no hover node.
		if ((bRightClick || bLeftClick) && (m_drawContext.hoverEntity == entt::null))
		{
			selections.clear();
		}
	}

	// Upadte rename state, only true when only one node selected.
	if (bDoubleClick && (selectedOneEntity != entt::null))
	{
		m_drawContext.bRenameing = true;
	}

	if (bRightClick)
	{
		ImGui::OpenPopup(m_drawContext.kHierarchyPopupMenuName);
	}
}

void WidgetHierarchy::popupMenu()
{
	auto activeWorld = m_worldManager->getActiveWorld();
	auto& selections = m_worldManagerUI->worldEntitySelections();

	if (!ImGui::BeginPopup(m_drawContext.kHierarchyPopupMenuName))
	{
		return;
	}

	ImGui::Separator();

	const bool bSelectedLessEqualOne = selections.num() <= 1;
	entt::entity selectedOneNode = entt::null;
	if (selections.num() == 1)
	{
		selectedOneNode = selections.getSelections()[0].entity;
	}

	if (selectedOneNode != entt::null)
	{
		static const std::string kRenameStr = combineIcon("Rename", ICON_FA_NONE);
		if (ImGui::MenuItem(kRenameStr.c_str()))
		{
			m_drawContext.bRenameing = true;
		}
	}

	if (selections.num() > 0)
	{
		static const std::string kDeleteStr = combineIcon("Delete", ICON_FA_NONE);
		if (ImGui::MenuItem(kDeleteStr.c_str()))
		{
			for (const auto& node : selections.getSelections())
			{
				auto entity = node.entity;
				if ((entity != entt::null) && !activeWorld->isRoot(entity))
				{
					activeWorld->deleteEntity(entity);
				}
			}
			selections.clear();

			ImGui::EndPopup();
			return;
		}

		ImGui::Separator();
	}

	if (bSelectedLessEqualOne)
	{
		const auto* camera = Flower::get().getActiveViewportCamera();
		math::dvec3 relativeCameraPos = math::dvec3(0);
		if (camera)
		{
			relativeCameraPos = camera->getPosition() + camera->getFront() * 5.0;
		}

		static const std::string kEmptyNodeStr = combineIcon("Empty Scene Node", ICON_FA_FAN);
		auto targetNode = (selectedOneNode == entt::null) ? activeWorld->getRoot() : selectedOneNode;

		if (ImGui::MenuItem(kEmptyNodeStr.c_str()))
		{
			auto newEntity = activeWorld->createEntity(u16str("Untitled"), targetNode, true, true);

			ImGui::EndPopup();
			return;
		}

		static const std::string kSkyName = combineIcon("Sky", ICON_FA_SUN);
		if (ImGui::MenuItem(kSkyName.c_str()))
		{
			auto newEntity = activeWorld->createEntity(u16str("Sky"), targetNode, true, true);
		}
	}

	ImGui::Separator();

	ImGui::EndPopup();
}

void WidgetHierarchy::beginDragDrop(entt::entity entity)
{
	auto  activeWorld = m_worldManager->getActiveWorld();
	auto& selections  = m_worldManagerUI->worldEntitySelections();

	if (ImGui::BeginDragDropSource())
	{
		m_drawContext.dragingEntities.reserve(selections.num());
		for (const auto& s : selections.getSelections())
		{
			m_drawContext.dragingEntities.push_back(s.entity);
		}

		ImGui::SetDragDropPayload(m_drawContext.kHierarchyDragDropName, &m_drawContext, sizeof(m_drawContext));
		ImGui::Text(activeWorld->getNameComponent(entity).name.u8().c_str());
		ImGui::EndDragDropSource();
	}
}

void WidgetHierarchy::acceptDragdrop(bool bRoot)
{
	auto& assetManager = Application::get().getAssetManager();
	auto  activeWorld  = m_worldManager->getActiveWorld();

	entt::entity targetEntity = entt::null;
	if (bRoot)
	{
		targetEntity = activeWorld->getRoot();
	}
	else
	{
		targetEntity = m_drawContext.hoverEntity;
	}

	if ((targetEntity != entt::null) && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_drawContext.kHierarchyDragDropName))
		{
			if (m_drawContext.dragingEntities.size() > 0)
			{
				for (const auto& dragingEntity : m_drawContext.dragingEntities)
				{
					if (dragingEntity != entt::null)
					{
						if (activeWorld->setParentRelationship(targetEntity, dragingEntity))
						{
							activeWorld->markDirty();

							m_drawContext.expandNodeInTreeView.insert(targetEntity);
							m_drawContext.expandNodeInTreeView.insert(dragingEntity);
						}
					}
				}

				// Reset draging node.
				m_drawContext.dragingEntities.clear();
			}
		}
		else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ProjectContentManager::getDragDropAssetsName()))
		{
			auto& dragDropAssets = Flower::get().getContentManager().getDragDropAssets();
			if (!dragDropAssets.selectAssets.empty())
			{
				for (const auto& asset : dragDropAssets.selectAssets)
				{
					if (asset.extension() == ".assetgltf")
					{
						if (auto gltfRef = assetManager.getOrLoadAsset<GLTFAsset>(asset, true))
						{
							const auto& gltfScene = gltfRef->getScene();
							const auto& gltfNodes = gltfRef->getNodes();
							const auto& gltfMeshes = gltfRef->getMeshes();

							std::function<void(entt::entity, const std::vector<int32>&)> buildNodeRecursive = [&](entt::entity parent, const std::vector<int32>& nodes) -> void
							{
								for (auto& nodeId : nodes)
								{
									const auto& activeGLTFNode = gltfNodes[nodeId];


									auto newWorldNode = activeWorld->createEntity(m_worldManagerUI->addUniqueIdForName(u16str(activeGLTFNode.name)), parent, true, true);

									buildNodeRecursive(newWorldNode, activeGLTFNode.childrenIds);
								}
							};


							auto sceneRootNode = activeWorld->createEntity(m_worldManagerUI->addUniqueIdForName(u16str("GLTFScene: " + gltfScene.name)), targetEntity);
							buildNodeRecursive(sceneRootNode, gltfScene.nodes);
						}
					}

				}

				// 
				dragDropAssets.consume();
			}
		}


		ImGui::EndDragDropTarget();
	}
}

void WidgetHierarchy::handleDrawState(entt::entity entity)
{
	auto activeWorld = m_worldManager->getActiveWorld();
	entt::registry& registry = activeWorld->getRegistry();

	const bool bVisibility = registry.view<ecs::VisibleTag>().contains(entity);
	const bool bStatic = registry.view<ecs::StaticTag>().contains(entity);

	const char* visibilityIcon = bVisibility ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
	const char* staticIcon = bStatic ? ICON_FA_PERSON : ICON_FA_PERSON_WALKING;

	auto iconSizeEye = ImGui::CalcTextSize(ICON_FA_EYE_SLASH);
	auto iconSizeStatic = ImGui::CalcTextSize(ICON_FA_PERSON_WALKING);


	ImGui::BeginGroup();
	ImGui::PushID((int)entity);


	auto eyePosX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - iconSizeEye.x - ImGui::GetFontSize() * 0.1f;
	ImGui::SetCursorPosX(eyePosX);
	if (ImGui::Selectable(visibilityIcon))
	{
		if (bVisibility)
		{
			registry.remove<ecs::VisibleTag>(entity);
		}
		else
		{
			registry.emplace<ecs::VisibleTag>(entity);
		}
	}
	ui::hoverTip("Set scene node visibility.");

	ImGui::SameLine();
	ImGui::SetCursorPosX(eyePosX - iconSizeEye.x);

	bool bSeletcted = false;
	if (ImGui::Selectable(staticIcon, &bSeletcted, ImGuiSelectableFlags_None, { iconSizeStatic.x, iconSizeEye.y }))
	{
		if (bStatic)
		{
			registry.remove<ecs::StaticTag>(entity);
		}
		else
		{
			registry.emplace<ecs::StaticTag>(entity);
		}
	}
	ui::hoverTip("Set scene node static state.");

	ImGui::PopID();
	ImGui::EndGroup();
}