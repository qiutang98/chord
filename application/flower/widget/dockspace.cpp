#include "dockspace.h"
#include "console.h"

using namespace chord;
using namespace chord::graphics;

MainViewportDockspaceAndMenu::MainViewportDockspaceAndMenu()
	: IWidget("MainViewportDockspaceAndMenu", "MainViewportDockspaceAndMenu")
{
    m_bShow = false;
}

void MainViewportDockspaceAndMenu::onTick(const chord::ApplicationTickData& tickData)
{
    ImGuiIO& io = ImGui::GetIO();
    if (!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable))
    {
        return;
    }

    static bool bShow = true;
    static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

    ImGuiWindowFlags windowFlags
        = ImGuiWindowFlags_MenuBar
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus;
    if (dockspaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
    {
        windowFlags |= ImGuiWindowFlags_NoBackground;
    }

    static std::string dockspaceName = "##Flower MainViewport Dockspace";
    auto viewport = ImGui::GetMainViewport();
    {
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f));
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin(dockspaceName.c_str(), &bShow, windowFlags);
            ImGui::PopStyleVar();

            ImGuiID dockspaceId = ImGui::GetID(dockspaceName.c_str());
            ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

            if (ImGui::BeginMenuBar())
            {
                drawDockspaceMenu();
                ImGui::EndMenuBar();
            }
        }
        ImGui::PopStyleVar(3);

        ImGui::End();
    }
}

void MainViewportDockspaceAndMenu::drawDockspaceMenu()
{
    if (ImGui::BeginMenu("  FILE  "))
    {

        ImGui::EndMenu();
    }
    ImGui::Separator();

    if (ImGui::BeginMenu("  EDIT  "))
    {

        ImGui::EndMenu();
    }
    ImGui::Separator(); 

    if (ImGui::BeginMenu("  VIEW  "))
    {
        widgetInView.loop([](WidgetInView& widgetInView) 
        {
            if(!widgetInView.bMultiWindow)
            {
                auto* widget = widgetInView.widgets[0];
                if (ImGui::MenuItem(widget->getName().c_str(), NULL, widget->getVisible()))
                {
                    widget->setVisible(!widget->getVisible());
                }
            }
            else
            {
                const char* name = widgetInView.widgets[0]->getWidgetName().c_str();
                if (ImGui::BeginMenu(name))
                {
                    for (size_t i = 0; i < widgetInView.widgets.size(); i++)
                    {
                        auto* widget = widgetInView.widgets[i];
                        std::string secondName = widgetInView.widgets[i]->getName();
                        if (ImGui::MenuItem(secondName.c_str(), NULL, widget->getVisible()))
                        {
                            widget->setVisible(!widget->getVisible());
                        }
                    }
                    ImGui::EndMenu();
                }
            }
        });
        ImGui::EndMenu();
    }
    ImGui::Separator();

    if (ImGui::BeginMenu("  HELP  "))
    {

        ImGui::EndMenu();
    }
    ImGui::Separator();
}