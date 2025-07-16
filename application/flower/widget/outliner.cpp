#include "outliner.h"
#include <ui/ui_helper.h>
#include "../flower.h"
#include "../manager/project_content.h"

using namespace chord;
using namespace chord::ui;


const char* WidgetOutliner::DrawContext::kPopupMenuName = "##OutlinerContextMenu";
const char* WidgetOutliner::DrawContext::kOutlinerDragDropName = "##OutlinerDragDropName";

static constexpr const char* kIconOutlinerTitle = ICON_FA_CHESS_QUEEN;

WidgetOutliner::WidgetOutliner()
	: IWidget(
		combineIcon("Outliner", kIconOutlinerTitle).c_str(),
		combineIcon("Outliner", kIconOutlinerTitle).c_str())
{
	m_sceneManager = &Application::get().getEngine().getSubsystem<SceneSubsystem>();
	m_sceneManagerUI = &Flower::get().getUISceneContentManager();
}


void WidgetOutliner::onInit()
{

}

void WidgetOutliner::onRelease()
{

}

void WidgetOutliner::onTick(const ApplicationTickData& tickData)
{

}

void WidgetOutliner::onVisibleTick(const ApplicationTickData& tickData)
{
	auto activeScene = m_sceneManager->getActiveScene();

	// Reset draw index.
	m_drawContext.drawIndex = 0;
	m_drawContext.hoverNode = { };

	// Header text.
	ImGui::Spacing();
	ImGui::TextDisabled("%s  Active scene:  %s.", ICON_FA_FAN, activeScene->getName().u8().c_str());
	ImGui::Spacing();
	ImGui::Separator();

	// 
	const float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("HierarchyScrollingRegion", ImVec2(0, -footerHeightToReserve), true, ImGuiWindowFlags_HorizontalScrollbar);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, m_drawContext.scenePaddingItemY));
	{
		for (auto& child : activeScene->getRootNode()->getChildren())
		{
			drawSceneNode(child);
		}

		handleEvent();
		popupMenu();
	}
	ImGui::PopStyleVar(1);
	ImGui::EndChild();

	acceptDragdrop(true);

	// End decorated text.
	ImGui::Separator(); ImGui::Spacing();
	ImGui::Text("  %d scene nodes.", activeScene->getNodeCount() - 1);

	m_drawContext.expandNodeInTreeView.clear();
}

void WidgetOutliner::drawSceneNode(std::shared_ptr<SceneNode> node)
{
	// This is an event draw or not.
	const bool bEvenDrawIndex = m_drawContext.drawIndex % 2 == 0;
	m_drawContext.drawIndex++;

	// This is a tree node or not.
	const bool bTreeNode = node->getChildren().size() > 0;

	const bool bVisibilityNodePrev = node->getVisibility();
	const bool bStaticNodePrev = node->getStatic();

	bool bEditingName = false;
	bool bPushEditNameDisable = false;
	bool bNodeOpen;
	bool bSelectedNode = false;

	auto& selections = m_sceneManagerUI->sceneNodeSelections();

	// Visible and static style prepare.
	if (!bVisibilityNodePrev) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.39f);
	if (!bStaticNodePrev) ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor::HSV(0.15f, 0.6f, 1.0f));
	{
		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanFullWidth;
		nodeFlags |= bTreeNode ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf;

		const bool bThisNodeSelected = selections.isSelected(SceneNodeSelctor(node));

		if (bThisNodeSelected)
		{
			nodeFlags |= ImGuiTreeNodeFlags_Selected;
		}

		auto& nodeNameUtf8 = node->getName();

		if (m_drawContext.bRenameing && bThisNodeSelected)
		{
			bNodeOpen = true;
			bEditingName = true;

			bPushEditNameDisable = true;
			ImGui::Indent();

			ImGui::Text("  %s  ", ICON_FA_ELLIPSIS);
			ImGui::SameLine();

			strcpy_s(m_drawContext.inputBuffer, nodeNameUtf8.u8().c_str());
			if (ImGui::InputText(" ", m_drawContext.inputBuffer, IM_ARRAYSIZE(m_drawContext.inputBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				std::string newName = m_drawContext.inputBuffer;
				if (!newName.empty())
				{
					node->setName(m_sceneManagerUI->addUniqueIdForName(u16str(newName)));
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
			if (m_drawContext.expandNodeInTreeView.contains(node->getId()))
			{
				ImGui::SetNextItemOpen(true);
			}

			bNodeOpen = ImGui::TreeNodeEx(
				reinterpret_cast<void*>(static_cast<intptr_t>(node->getId())),
				nodeFlags,
				" %s    %s", bTreeNode ? ICON_FA_FOLDER : ICON_FA_FAN,
				nodeNameUtf8.u8().c_str());
		}

		// update hover node.
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
		{
			m_drawContext.hoverNode = node;
		}

		if (ImGui::IsItemClicked())
		{
			if (ImGui::GetIO().KeyCtrl)
			{
				if (selections.isSelected(SceneNodeSelctor(m_drawContext.hoverNode.lock())))
				{
					selections.remove(SceneNodeSelctor(m_drawContext.hoverNode.lock()));
				}
				else
				{
					selections.add(SceneNodeSelctor(m_drawContext.hoverNode.lock()));
				}
			}
			else
			{
				selections.clear();
				selections.add(SceneNodeSelctor(m_drawContext.hoverNode.lock()));
			}
		}

		// start drag drop.
		beginDragDrop(node);
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
			handleDrawState(node);
		}

	}

	// Visible and static style pop.
	if (!bVisibilityNodePrev) ImGui::PopStyleVar();
	if (!bStaticNodePrev) ImGui::PopStyleColor();

	if (bNodeOpen)
	{
		if (bTreeNode)
		{
			for (const auto& child : node->getChildren())
			{
				drawSceneNode(child);
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

void WidgetOutliner::handleEvent()
{
	const auto bWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if (!bWindowHovered)
	{
		return;
	}

	auto& selections = m_sceneManagerUI->sceneNodeSelections();

	// Prepare seleted one node state.
	std::shared_ptr<SceneNode> selectedOneNode = nullptr;
	if (selections.num() == 1)
	{
		selectedOneNode = selections.getSelections()[0].node.lock();
	}

	const auto bLeftClick = ImGui::IsMouseClicked(0);
	const auto bRightClick = ImGui::IsMouseClicked(1);
	const auto bDoubleClick = ImGui::IsMouseDoubleClicked(0);

	// Click empty state.
	if (!ImGui::GetIO().KeyCtrl)
	{
		// Update selected node to root if no hover node.
		if ((bRightClick || bLeftClick) && !m_drawContext.hoverNode.lock())
		{
			selections.clear();
		}
	}

	// Upadte rename state, only true when only one node selected.
	if (bDoubleClick && selectedOneNode)
	{
		m_drawContext.bRenameing = true;
	}

	if (bRightClick)
	{
		ImGui::OpenPopup(m_drawContext.kPopupMenuName);
	}
}

void WidgetOutliner::popupMenu()
{
	auto activeScene = m_sceneManager->getActiveScene();
	auto& selections = m_sceneManagerUI->sceneNodeSelections();

	if (!ImGui::BeginPopup(m_drawContext.kPopupMenuName))
	{
		return;
	}

	ImGui::Separator();

	const bool bSelectedLessEqualOne = selections.num() <= 1;
	std::shared_ptr<SceneNode> selectedOneNode = nullptr;
	if (selections.num() == 1)
	{
		selectedOneNode = selections.getSelections()[0].node.lock();
	}

	if (selectedOneNode)
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
				auto nodePtr = node.node.lock();
				if (nodePtr && !nodePtr->isRoot())
				{
					activeScene->deleteNode(node.node.lock());
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

		if (ImGui::MenuItem(kEmptyNodeStr.c_str()))
		{
			auto newNode = activeScene->createNode(m_sceneManagerUI->addUniqueIdForName(u16str("Untitled")), selectedOneNode);
			newNode->getTransform()->setTranslation(relativeCameraPos);

			ImGui::EndPopup();
			return;
		}
	}

	ImGui::Separator();

	ImGui::EndPopup();
}

void WidgetOutliner::beginDragDrop(std::shared_ptr<SceneNode> node)
{
	auto& selections = m_sceneManagerUI->sceneNodeSelections();

	if (ImGui::BeginDragDropSource())
	{
		m_drawContext.dragingNodes.reserve(selections.num());
		for (const auto& s : selections.getSelections())
		{
			m_drawContext.dragingNodes.push_back(s.node);
		}

		ImGui::SetDragDropPayload(m_drawContext.kOutlinerDragDropName, &m_drawContext, sizeof(m_drawContext));
		ImGui::Text(node->getName().u8().c_str());
		ImGui::EndDragDropSource();
	}
}

void WidgetOutliner::acceptDragdrop(bool bRoot)
{
	auto& assetManager = Application::get().getAssetManager();
	auto activeScene = m_sceneManager->getActiveScene();
	SceneNodeRef targetNode = nullptr;
	if (bRoot)
	{
		targetNode = activeScene->getRootNode();
	}
	else
	{
		targetNode = m_drawContext.hoverNode.lock();
	}

	if (targetNode && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(m_drawContext.kOutlinerDragDropName))
		{
			if (m_drawContext.dragingNodes.size() > 0)
			{
				for (const auto& node : m_drawContext.dragingNodes)
				{
					if (auto dragingNode = node.lock())
					{
						if (activeScene->setParent(targetNode, dragingNode))
						{
							activeScene->markDirty();

							m_drawContext.expandNodeInTreeView.insert(targetNode->getId());
							m_drawContext.expandNodeInTreeView.insert(dragingNode->getId());
						}
					}
				}

				// Reset draging node.
				m_drawContext.dragingNodes.clear();
				sortChildren(targetNode);
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
							const auto& gltfScene  = gltfRef->getScene();
							const auto& gltfNodes  = gltfRef->getNodes();
							const auto& gltfMeshes = gltfRef->getMeshes();


							std::function<void(SceneNodeRef, const std::vector<int32>&)> buildNodeRecursive = [&](SceneNodeRef parent, const std::vector<int32>& nodes) -> void
							{
								for (auto& nodeId : nodes)
								{
									const auto& activeGLTFNode = gltfNodes[nodeId];

									auto newSceneNode =
										activeScene->createNode(m_sceneManagerUI->addUniqueIdForName(u16str(activeGLTFNode.name)), parent);

									// WARN: Precision lose. dmat->mat
									newSceneNode->getTransform()->setMatrix(activeGLTFNode.localMatrix);

									// 
									if (activeGLTFNode.mesh > -1)
									{
										auto gltfMeshComp = 
											std::dynamic_pointer_cast<GLTFMeshComponent>(GLTFMeshComponent::kComponentUIDrawDetails.factory());
										activeScene->addComponent<GLTFMeshComponent>(gltfMeshComp, newSceneNode);
										gltfMeshComp->setGLTFMesh(gltfRef->getSaveInfo(), activeGLTFNode.mesh);

										std::vector<AssetSaveInfo> materials{};
										for (auto& primitive : gltfMeshes[activeGLTFNode.mesh].primitives)
										{
											materials.push_back(primitive.material);
										}
										gltfMeshComp->setGLTFMaterial(materials);
									}
									

									buildNodeRecursive(newSceneNode, activeGLTFNode.childrenIds);
								}
							};


							auto sceneRootNode = activeScene->createNode(m_sceneManagerUI->addUniqueIdForName(u16str("GLTFScene: " + gltfScene.name)), targetNode);
							buildNodeRecursive(sceneRootNode, gltfScene.nodes);
						}
					}

				}

				// 
				dragDropAssets.consume();
				sortChildren(targetNode);
			}
		}


		ImGui::EndDragDropTarget();
	}
}

void WidgetOutliner::handleDrawState(std::shared_ptr<SceneNode> node)
{
	const char* visibilityIcon = node->getVisibility() ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
	const char* staticIcon = node->getStatic() ? ICON_FA_PERSON : ICON_FA_PERSON_WALKING;

	auto iconSizeEye = ImGui::CalcTextSize(ICON_FA_EYE_SLASH);
	auto iconSizeStatic = ImGui::CalcTextSize(ICON_FA_PERSON_WALKING);


	ImGui::BeginGroup();
	ImGui::PushID(node->getId());

	
	auto eyePosX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - iconSizeEye.x - ImGui::GetFontSize() * 0.1f;
	ImGui::SetCursorPosX(eyePosX);
	if (ImGui::Selectable(visibilityIcon))
	{
		node->setVisibility(!node->getVisibility());
	}
	ui::hoverTip("Set scene node visibility.");

	ImGui::SameLine();
	ImGui::SetCursorPosX(eyePosX - iconSizeEye.x);

	bool bSeletcted = false;
	if (ImGui::Selectable(staticIcon, &bSeletcted, ImGuiSelectableFlags_None, { iconSizeStatic.x, iconSizeEye.y }))
	{
		node->setStatic(!node->getStatic());
	}
	ui::hoverTip("Set scene node static state.");

	ImGui::PopID();
	ImGui::EndGroup();
}

void WidgetOutliner::sortChildren(std::shared_ptr<SceneNode> node)
{
	if (node)
	{
		auto& children = node->getChildren();

		std::sort(std::begin(children), std::end(children), [&](const auto& a, const auto& b)
		{
			return a->getName() < b->getName();
		});

		for (auto& child : children)
		{
			sortChildren(child);
		}
	}
}