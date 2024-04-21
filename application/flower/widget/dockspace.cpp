#include "dockspace.h"
#include "console.h"

#include <asset/asset.h>

using namespace chord;
using namespace chord::graphics;

MainViewportDockspaceAndMenu::MainViewportDockspaceAndMenu()
	: IWidget("MainViewportDockspaceAndMenu", "MainViewportDockspaceAndMenu")
    , contentAssetImport(combineIcon("Imported assets config...", ui::fontIcon::message2))
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


    contentAssetImport.draw();
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

ContentAssetImportWidget::ContentAssetImportWidget(const std::string& titleName)
    : ImGuiPopupSelfManagedOpenState(titleName, ImGuiWindowFlags_AlwaysAutoResize)
{

}

void ContentAssetImportWidget::onDraw()
{
    if (m_bImporting)
    {
        onDrawImporting();
    }
    else
    {
        onDrawState();
    }
}

void ContentAssetImportWidget::onDrawState()
{
    check(!importConfigs.empty());
    check(!typeName.empty());

    const auto* meta = Application::get().getAssetManager().getRegisteredAssetMap().at(typeName);
    for (auto& ptr : importConfigs)
    {
        meta->uiDrawAssetImportConfig(ptr);
    }

    bool bAccept = false;

    if (ImGui::Button("OK", ImVec2(120, 0)))
    {
        m_bImporting = true;
        {
            check(!m_importProgress.logHandle.isValid());
            {
                m_importProgress.logHandle = LoggerSystem::get().pushCallback([&](const std::string& info, ELogType type)
                {
                    m_importProgress.logItems.push_back({ type, info });
                    if (static_cast<uint32_t>(m_importProgress.logItems.size()) >= 60)
                    {
                        m_importProgress.logItems.pop_front();
                    }
                });
            }

            check(m_executeFutures.futures.empty());
            {
                const auto loop = [this, meta](const size_t loopStart, const size_t loopEnd)
                {
                    for (size_t i = loopStart; i < loopEnd; ++i)
                    {
                        if (!meta->importAssetFromConfig(importConfigs[i]))
                        {
                            LOG_ERROR("Import asset from '{}' to '{}' failed.",
                                utf8::utf16to8(importConfigs[i]->importFilePath.u16string()),
                                utf8::utf16to8(importConfigs[i]->storeFilePath.u16string()));
                        }
                    }
                };
                m_executeFutures = Application::get().getThreadPool().parallelizeLoop(0, importConfigs.size(), loop);
            }
        }
    }

    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        bAccept = true;
        ImGui::CloseCurrentPopup();
    }

    if (bAccept)
    {
        if (afterEventAccept)
        {
            afterEventAccept();
        }
        onClosed();
    }
}

void ContentAssetImportWidget::onDrawImporting()
{
    check(!m_executeFutures.futures.empty());
    check(m_bImporting);

    ImGui::Indent();
    ImGui::Text("Asset  Importing ...    ");
    ImGui::SameLine();

    float progress = m_executeFutures.getProgress();
    ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));

    ImGui::Unindent();
    ImGui::Separator();

    ImGui::BeginDisabled();
    for (int i = 0; i < m_importProgress.logItems.size(); i++)
    {
        ImVec4 color;
        if (m_importProgress.logItems[i].first == ELogType::Error ||
            m_importProgress.logItems[i].first == ELogType::Fatal)
        {
            color = ImVec4(1.0f, 0.08f, 0.08f, 1.0f);
        }
        else if (m_importProgress.logItems[i].first == ELogType::Warn)
        {
            color = ImVec4(1.0f, 1.0f, 0.1f, 1.0f);
        }
        else
        {
            color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }


        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Selectable(m_importProgress.logItems[i].second.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();

    bool bAccept = false;
    if (progress > 0.99f)
    {
        m_executeFutures.wait();

        // Clean state.
        m_bImporting = false;
        m_executeFutures = {};
        if (m_importProgress.logHandle.isValid())
        {
            LoggerSystem::get().popCallback(m_importProgress.logHandle);
            m_importProgress.logHandle = {};
            m_importProgress.logItems.clear();
        }

        bAccept = true;
        ImGui::CloseCurrentPopup();
    }

    if (bAccept)
    {
        if (afterEventAccept)
        {
            afterEventAccept();
        }
        onClosed();
    }
}
