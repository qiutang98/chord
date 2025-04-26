#include "dockspace.h"
#include "console.h"
#include "../manager/project_content.h"
#include "../flower.h"

#include <project.h>
#include <asset/asset.h>
#include <scene/scene_manager.h>
#include <nfd.h>

using namespace chord;
using namespace chord::graphics;

MainViewportDockspaceAndMenu::MainViewportDockspaceAndMenu()
	: IWidget("MainViewportDockspaceAndMenu", "MainViewportDockspaceAndMenu")
    , sceneAssetSave(combineIcon("Save edited scenes...", ICON_FA_MESSAGE))
    , contentAssetImport(combineIcon("Imported assets config...", ICON_FA_MESSAGE))
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

    sceneAssetSave.draw();
    contentAssetImport.draw();
}

void MainViewportDockspaceAndMenu::drawDockspaceMenu()
{
    if (ImGui::BeginMenu("  FILE  "))
    {
        auto& projectContent = Flower::get().getContentManager();
        const bool bSceneDirty = !projectContent.getDirtyAsset<Scene>().empty();
        if (ImGui::MenuItem(combineIcon("New Scene", ICON_FA_NONE).c_str()))
        {
            if (bSceneDirty)
            {
                if (sceneAssetSave.open())
                {
                    check(!sceneAssetSave.afterEventAccept);
                    sceneAssetSave.afterEventAccept = []()
                    {
                        auto& sceneManager = Application::get().getEngine().getSubsystem<SceneManager>();
                        sceneManager.releaseScene();
                    };
                }
            }
            else
            {
                Application::get().getEngine().getSubsystem<SceneManager>().releaseScene();
            }
        }

        if (!bSceneDirty) { ImGui::BeginDisabled(); }
        if (ImGui::MenuItem(combineIcon("Save Scene", ICON_FA_NONE).c_str()))
        {
            if (bSceneDirty)
            {
                sceneAssetSave.open();
            }
        }
        if (!bSceneDirty) { ImGui::EndDisabled(); }

        ImGui::Separator();

        if (ImGui::MenuItem(combineIcon(" Exit", ICON_FA_POWER_OFF).c_str()))
        {
            Application::get().close();
        }



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
        if (ImGui::MenuItem(combineIcon(" About", ICON_FA_CIRCLE_QUESTION).c_str()))
        {

        }

        ImGui::Separator();

        if (ImGui::MenuItem(combineIcon("Developer", ICON_FA_NONE).c_str()))
        {

        }

        if (ImGui::MenuItem(combineIcon("SDKs", ICON_FA_NONE).c_str()))
        {

        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
}

SceneAssetSaveWidget::SceneAssetSaveWidget(const std::string& titleName)
    : ImGuiPopupSelfManagedOpenState(titleName, ImGuiWindowFlags_AlwaysAutoResize)
{

}

void SceneAssetSaveWidget::onDraw()
{
    auto& projectContent = Flower::get().getContentManager();
    auto& sceneManager = Application::get().getEngine().getSubsystem<SceneManager>();

    auto scenes = projectContent.getDirtyAsset<Scene>();

    // Current only support one scene edit.
    check(scenes.size() == 1);
    auto& scene = scenes[0];

    const bool bTemp = scene->getSaveInfo().isTemp();

    if (m_processingAsset != scene->getSaveInfo())
    {
        m_processingAsset = scene->getSaveInfo();
        m_bSelected = true;
    }



    ImGui::TextDisabled("Scene still un-save after edited, please decide discard or save.");
    ImGui::NewLine();
    ImGui::Indent();
    {
        std::string showName = scene->kAssetTypeMeta.decoratedName + ":  " + scene->getName().u8() +
            (bTemp ? "*  (Created)" : "*  (Edited)");

        if (bTemp) ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor::HSV(0.15f, 0.6f, 1.0f));
        {
            ImGui::Checkbox("##HideCheckBox", &m_bSelected); ImGui::SameLine();
            ImGui::Selectable(showName.c_str(), &m_bSelected, ImGuiSelectableFlags_DontClosePopups);
        }
        if (bTemp) ImGui::PopStyleColor();
    }
    ImGui::Unindent();
    ImGui::NewLine();
    ImGui::NewLine();
    ImGui::NewLine();

    bool bAccept = false;
    const auto buttonSize = ImVec2(6.0f * ImGui::GetFontSize(), 0.0f);
    float curStartX = ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x * 0.5f - (buttonSize.x + ImGui::GetItemSpacing() * 0.5f);
    ImGui::SetCursorPosX(curStartX);
    if (ImGui::Button("Save", buttonSize))
    {
        bAccept = true;
        if (m_bSelected)
        {
            if (bTemp)
            {
                std::string path;

   
                std::string assetStartFolder = Project::get().getPath().assetPath.u8();
                std::string suffix = std::string(scene->kAssetTypeMeta.suffix).erase(0, 1) + "\0";

                nfdchar_t* outPathChars;

                nfdchar_t* filterList = suffix.data();
                nfdresult_t result = NFD_SaveDialog(filterList, assetStartFolder.c_str(), &outPathChars);
                if (result == NFD_OKAY)
                {
                    path = outPathChars;
                    free(outPathChars);
                }

                auto u16PathString = utf8::utf8to16(path);
                std::filesystem::path fp(u16PathString);
                if (!path.empty())
                {
                    std::filesystem::path assetName = fp.filename();
                    std::string assetNameUtf8 = utf8::utf16to8(assetName.u16string()) + scene->kAssetTypeMeta.suffix;

                    const auto relativePath = buildRelativePath(Project::get().getPath().assetPath.u16(), fp.remove_filename());

                    const AssetSaveInfo newInfo(u16str(assetNameUtf8), relativePath);
                    Application::get().getAssetManager().changeSaveInfo(newInfo, scene);

                    if (!scene->save())
                    {
                        LOG_ERROR("Fail to save new created scene {0} in path {1}.", 
                            scene->getName().u8(), 
                            utf8::utf16to8(scene->getStorePath().u16string()));
                    }
                }
            }
            else
            {
                if (!scene->save())
                {
                    LOG_ERROR("Fail to save edited scene {0} in path {1}.", 
                        scene->getName().u8(),
                        utf8::utf16to8(scene->getStorePath().u16string()));
                }
            }
        }

        m_bSelected = true;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Discard", buttonSize))
    {
        bAccept = true;
        if (m_bSelected)
        {
            const auto saveInfo = scene->getSaveInfo();
            Application::get().getAssetManager().unload(scene, true);

            if (!saveInfo.isTemp())
            {
                // Reload src scene in disk.
                sceneManager.releaseScene();
                sceneManager.loadScene(saveInfo.path());
            }
        }
        m_bSelected = true;
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

    ImGui::Spacing();
    ImGui::Spacing();
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

constexpr size_t kMaxImportLogItemsCount = 60;

void ContentAssetImportWidget::onDrawState()
{
    check(!importConfigs.empty());
    check(!typeName.empty());

    const auto* meta = Application::get().getAssetManager().getRegisteredAssetMap().at(typeName);
    for (auto& ptr : importConfigs)
    {
        ImGui::Spacing();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushID(std::hash<std::filesystem::path>{}(ptr->storeFilePath));
        ImGui::Indent();
        {
            std::string utf8Name = utf8::utf16to8(ptr->importFilePath.u16string());
            std::string saveUtf8 = utf8::utf16to8(ptr->storeFilePath.u16string());

            ImGui::TextDisabled(std::format("Load from: {}", utf8Name).c_str());
            ImGui::TextDisabled(std::format("Save to: {}", saveUtf8).c_str());
            ImGui::Spacing();

            meta->importConfig.uiDrawAssetImportConfig(ptr);
        }
        ImGui::Unindent();
        ImGui::PopStyleVar();
        ImGui::PopID();
        ImGui::NewLine();
        ImGui::Separator();
    }

    bool bAccept = false;
    const auto buttonSize = ImVec2(8.0f * ImGui::GetFontSize(), 0.0f);

    

    float curStartX = ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x * 0.5f - (buttonSize.x + ImGui::GetItemSpacing() * 0.5f);
    ImGui::SetCursorPosX(curStartX);

    if (ImGui::Button("OK", buttonSize))
    {
        m_bImporting = true;
        {
            check(!m_importProgress.logHandle);
            {
                m_importProgress.logHandle = LoggerSystem::get().pushCallback([&](const std::string& info, ELogLevel type)
                {
                    std::lock_guard lock(m_importProgress.asyncLogLock);

                    m_importProgress.asyncLogItems.push_back({ type, info });
                    if (static_cast<uint32_t>(m_importProgress.asyncLogItems.size()) >= kMaxImportLogItemsCount)
                    {
                        m_importProgress.asyncLogItems.pop_front();
                    }
                });
            }

            check(m_executeFutures.isEmpty());
            {
				for (uint32 i = 0; i < importConfigs.size(); i++)
				{
                    m_executeFutures.add(chord::jobsystem::launch(EJobFlags::None, [meta, config = importConfigs[i]]()
                    {
                        if (!meta->importConfig.importAssetFromConfig(config))
                        {
                            LOG_ERROR("Import asset from '{}' to '{}' failed.",
                                utf8::utf16to8(config->importFilePath.u16string()),
                                utf8::utf16to8(config->storeFilePath.u16string()));
                        }
                    }));
				}
            }
        }
    }

    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", buttonSize))
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
    ImGui::Spacing();
    ImGui::Spacing();
}

void ContentAssetImportWidget::onDrawImporting()
{
    check(!m_executeFutures.isEmpty());
    check(m_bImporting);

    ImGui::Indent();
    ImGui::Text("Asset  Importing ...    ");
    ImGui::SameLine();

    float progress = m_executeFutures.getProgress();
    ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));

    ImGui::Unindent();
    ImGui::Separator();

    ImGui::BeginDisabled();

    {
        std::lock_guard lock(m_importProgress.asyncLogLock);
        for (const auto& logItem : m_importProgress.asyncLogItems)
        {
            m_importProgress.logItems.push_back(logItem);
            if (static_cast<uint32_t>(m_importProgress.logItems.size()) >= kMaxImportLogItemsCount)
            {
                m_importProgress.logItems.pop_front();
            }
        }
    }

    for (int i = 0; i < m_importProgress.logItems.size(); i++)
    {
        ImVec4 color;
        if (m_importProgress.logItems[i].first == ELogLevel::Error ||
            m_importProgress.logItems[i].first == ELogLevel::Fatal)
        {
            color = ImVec4(1.0f, 0.08f, 0.08f, 1.0f);
        }
        else if (m_importProgress.logItems[i].first == ELogLevel::Warn)
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
        m_executeFutures.wait(EBusyWaitType::None);
        m_executeFutures.clear();

        // Clean state.
        m_bImporting = false;
        if (m_importProgress.logHandle)
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
