#include <scene/manager/scene_manager.h>
#include <ui/ui_helper.h>

namespace chord
{
    ISceneManager::ISceneManager(const char* name, const std::string& decoratedName)
        : m_name(FName(name, true))
        , m_decoratedName(decoratedName)
    {

    }

    void ISceneManager::drawUI(SceneRef scene)
    {
        const ImGuiTreeNodeFlags treeNodeFlags =
            ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_Framed |
            ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_AllowItemOverlap |
            ImGuiTreeNodeFlags_FramePadding;

        ImGui::PushID(m_name.getHashId());
        ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4.0f * ImGui::GetWindowDpiScale(), 4.0f * ImGui::GetWindowDpiScale() });
        bool open = ImGui::TreeNodeEx("TreeNodeForComp", treeNodeFlags, m_decoratedName.c_str());
        ImGui::PopStyleVar();

        if (open)
        {
            ImGui::PushID("Widget");
            ImGui::Spacing();

            onDrawUI(scene);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PopID();

            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}