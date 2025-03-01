#include "app/app.h"
#include "gpu/gpu.h"
#include "gpu/async_upload.h"
#include "font/font.h"
#include "common/common.h"
#include "traverser/traverser.h"
#include "build_number.h"
#include "common/ui_shared.h"
#include "compositor/compositor.h"
#include "node_category/node_category.h"
#include "dispatchers_installer/dispatchers_installer.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_freetype.h"
#include "../avcpp/av.h"
#include "../avcpp/ffmpeg.h"
#include "../avcpp/avutils.h"

using namespace av;

namespace Raster {

    std::vector<AbstractUI> App::s_windows{};

    void App::Initialize() {
        static NFD::Guard s_guard;
        av::init();
        av::set_logging_level(AV_LOG_ERROR);
        std::cout << "ffmpeg version: " << av::getversion() << std::endl;

        GPU::Initialize();
        AsyncUpload::Initialize();
        ImGui::SetCurrentContext((ImGuiContext*) GPU::GetImGuiContext());

        Workspace::s_configuration = Configuration(ReadJson("misc/config.json"));

        try {
            Localization::Load(ReadJson(FormatString("misc/localizations/%s.json", Workspace::s_configuration.localizationCode.c_str())));
        } catch (std::exception ex) {
            Localization::Load(ReadJson("misc/localizations/en.json"));
        }

        DefaultNodeCategories::Initialize();
        Workspace::Initialize();

        ImGuiIO& io = ImGui::GetIO();

        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImFontConfig fontCfg = {};

        static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

        Font::s_normalFont = io.Fonts->AddFontFromMemoryCompressedTTF(
            Font::s_fontBytes.data(), Font::s_fontSize,
            16.0f, &fontCfg, io.Fonts->GetGlyphRangesCyrillic()
        );

        fontCfg.MergeMode = true;
        io.Fonts->AddFontFromMemoryCompressedTTF(
            Font::s_fontAwesomeBytes.data(), Font::s_fontAwesomeSize,
            16.0f * 0.85f, &fontCfg, icons_ranges
        );

        fontCfg.RasterizerDensity = 5;

        fontCfg.MergeMode = false;
        Font::s_denseFont = io.Fonts->AddFontFromMemoryCompressedTTF(
                    Font::s_fontBytes.data(), Font::s_fontSize,
                    16.0f, &fontCfg, io.Fonts->GetGlyphRangesCyrillic());

        fontCfg.MergeMode = true;
        io.Fonts->AddFontFromMemoryCompressedTTF(
            Font::s_fontAwesomeBytes.data(), Font::s_fontAwesomeSize,
            16.0f * 0.85f, &fontCfg, icons_ranges
        ); 

        DispatchersInstaller::Initialize();
        Compositor::Initialize();

        auto& style = ImGui::GetStyle();
        // style.CurveTessellationTol = 0.01f;
        style.ScrollSmooth = 4;
        style.SeparatorTextAlign = ImVec2(0.5, 0.5);
        style.ScrollbarSize = 10;
	
        style.Alpha = 1.0f;
        style.DisabledAlpha = 0.7f;
        style.WindowPadding = ImVec2(12, 12);
        style.WindowRounding = 2;
        style.WindowBorderSize = 1.0f;
        style.WindowMinSize = ImVec2(20.0f, 20.0f);
        style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Right;
        style.ChildRounding = 0;
        style.ChildBorderSize = 1.0f;
        style.PopupRounding = 2.0f;
        style.PopupBorderSize = 1.0f;
        style.FramePadding = ImVec2(8.0f, 3);
        style.FrameRounding = 2;
        style.FrameBorderSize = 1.0f;
        style.ItemSpacing = ImVec2(4, 6);
        style.ItemInnerSpacing = ImVec2(6, 6);
        style.CellPadding = ImVec2(12.10000038146973f, 2);
        style.IndentSpacing = 21.0f;
        style.ColumnsMinSpacing = 4.900000095367432f;
        style.ScrollbarSize = 16;
        style.ScrollbarRounding = 16;
        style.GrabMinSize = 8;
        style.GrabRounding = 0;
        style.TabRounding = 0.0f;
        style.TabBorderSize = 0.0f;
        style.TabBarBorderSize = 0.0f;
        style.TabMinWidthForCloseButton = 0.0f;
        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0, 0.0f);
        style.DockingSeparatorSize = 4;
        
        ImVec4* colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Text]                   = ImVec4(0.87f, 0.87f, 0.87f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.68f, 0.68f, 0.68f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.07f, 0.07f, 0.07f, 0.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
        colors[ImGuiCol_Border]                 = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.08f, 0.09f, 0.10f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.91f, 0.91f, 0.91f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.21f, 0.21f, 0.21f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.48f, 0.48f, 0.48f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0, 0, 0, 1.00f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
        colors[ImGuiCol_DockingPreview]         = ImVec4(0.98f, 0.98f, 0.98f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.71f, 0.71f, 0.71f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.04f, 0.98f, 0.98f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.88f, 0.80f, 0.56f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.66f, 0.66f, 0.66f, 0.50f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.55f, 0.55f, 0.55f, 0.50f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.55f, 0.55f, 0.55f, 0.50f);



        s_windows.push_back(UIFactory::SpawnDockspaceUI());
        s_windows.push_back(UIFactory::SpawnNodeGraphUI());
        s_windows.push_back(UIFactory::SpawnNodePropertiesUI());
        s_windows.push_back(UIFactory::SpawnRenderingUI());
        s_windows.push_back(UIFactory::SpawnTimelineUI());
        s_windows.push_back(UIFactory::SpawnAssetManagerUI());
        s_windows.push_back(UIFactory::SpawnEasingEditor());
    }

    void App::RenderLoop() {
        while (!GPU::MustTerminate()) {
            UIShared::s_timelineAnykeyframeDragged = false;

            std::string constructedTitle = "Raster - Build Number " + std::to_string(BUILD_NUMBER);
            if (Workspace::s_project.has_value()) {
                auto& project = Workspace::s_project.value();
                constructedTitle += " - " + project.name;
            }
            GPU::SetWindowTitle(constructedTitle);

            GPU::BeginFrame();
                if (Workspace::s_project.has_value() && ImGui::IsAnyItemActive() == false && ImGui::IsAnyItemFocused() == false && ImGui::IsKeyPressed(ImGuiKey_Space)) {
                    Workspace::GetProject().playing = !Workspace::GetProject().playing;
                }
                if (Workspace::s_project.has_value()) {
                    auto& project = Workspace::GetProject();
                    if (project.playing) {
                        auto projectLength = project.GetProjectLength();
                        if (project.currentFrame >= projectLength) {
                            if (project.looping) {
                                project.currentFrame = 0;
                            } else project.playing = false;
                        } else {
                            if (project.currentFrame < projectLength) {
                                project.currentFrame += (project.framerate * ImGui::GetIO().DeltaTime);
                            }
                        }
                    }
                }
                GPU::BindFramebuffer(std::nullopt);
                Compositor::s_bundles.clear();
                Compositor::EnsureResolutionConstraints();
                if (Workspace::s_project.has_value()) {
                    auto& project = Workspace::s_project.value();
                    project.currentFrame = std::max(project.currentFrame, 0.0f);
                    project.currentFrame = std::min(project.currentFrame, project.GetProjectLength());
                }
                Traverser::TraverseAll();

                for (const auto& window : s_windows) {
                    window->Render();
                }

                ImGui::ShowDemoWindow();

                Compositor::PerformComposition();
                GPU::BindFramebuffer(std::nullopt);
            GPU::EndFrame();
        }
    }

    void App::Terminate() {
        if (Workspace::s_project.has_value()) {
            Workspace::GetProject().compositions.clear();
        }
        AsyncUpload::Terminate();
        GPU::Terminate();
    }
}