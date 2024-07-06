#include "common/common.h"
#include "font/font.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_drag.h"
#include "gpu/gpu.h"

namespace Raster {

    static ImVec2 FitRectInRect(ImVec2 dst, ImVec2 src) {
        float scale = std::min(dst.x / src.x, dst.y / src.y);
        return ImVec2{src.x * scale, src.y * scale};
    }

    static std::vector<std::string> SplitString(std::string s, std::string delimiter) {
        size_t pos_start = 0, pos_end, delim_len = delimiter.length();
        std::string token;
        std::vector<std::string> res;

        while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
            token = s.substr (pos_start, pos_end - pos_start);
            pos_start = pos_end + delim_len;
            res.push_back (token);
        }

        res.push_back (s.substr (pos_start));
        return res;
    }

    void PreviewDispatchers::DispatchStringValue(std::any& t_attribute) {
        static DragStructure textDrag;
        static ImVec2 textOffset;
        static float zoom = 1.0f;

        std::string str = std::any_cast<std::string>(t_attribute);
        if (zoom > 1) {
            ImGui::PushFont(Font::s_denseFont);
        }
        ImGui::SetWindowFontScale(zoom);
            ImVec2 textSize = ImGui::CalcTextSize(str.c_str());
            ImGui::SetCursorPos(ImVec2{
                ImGui::GetContentRegionAvail().x / 2.0f - textSize.x / 2.0f,
                ImGui::GetContentRegionAvail().y / 2.0f - textSize.y / 2.0f
            } + textOffset);
            ImGui::Text(str.c_str());
        ImGui::SetWindowFontScale(1.0f);
        if (zoom > 1) {
            ImGui::PopFont();
        }

        int lines = SplitString(str, "\n").size();
        std::string footerText = FormatString("%i %s; %i %s; UTF-8", (int) str.size(), Localization::GetString("CHARS").c_str(), lines, Localization::GetString("LINES").c_str());
        ImVec2 footerSize = ImGui::CalcTextSize(footerText.c_str());
        ImGui::SetCursorPos({
            ImGui::GetWindowSize().x / 2.0f - footerSize.x / 2.0f,
            ImGui::GetWindowSize().y - footerSize.y - ImGui::GetStyle().WindowPadding.x
        });
        ImGui::Text(footerText.c_str());

        textDrag.Activate();
        float textDragDistance;
        if (textDrag.GetDragDistance(textDragDistance)) {
            textOffset = textOffset + ImGui::GetIO().MouseDelta;
        } else textDrag.Deactivate();

        if (ImGui::GetIO().MouseWheel != 0 && ImGui::IsWindowFocused()) {
            zoom += ImGui::GetIO().MouseWheel * 0.1f;
            zoom = std::max(zoom, 0.5f);
        }

        if (ImGui::IsWindowFocused() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("##stringPreviewPopup");
        } 
        if (ImGui::BeginPopup("##stringPreviewPopup")) {
            ImGui::SeparatorText(FormatString("%s %s", ICON_FA_FONT, Localization::GetString("ATTRIBUTE").c_str()).c_str());
            if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_FONT, Localization::GetString("REVERT_VIEW").c_str()).c_str())) {
                textOffset = {0, 0};
                zoom = 1.0f;
            }
            ImGui::EndPopup();
        }
    }

    void PreviewDispatchers::DispatchTextureValue(std::any& t_attribute) {
        static DragStructure imageDrag;
        static ImVec2 imageOffset;
        static float zoom = 1.0f;

        Texture texture = std::any_cast<Texture>(t_attribute);
        ImVec2 fitTextureSize = FitRectInRect(ImGui::GetWindowSize(), ImVec2(texture.width, texture.height));

        ImGui::BeginChild("##imageContainer", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetCursorPos(ImVec2{
                ImGui::GetWindowSize().x / 2.0f - fitTextureSize.x * zoom / 2,
                ImGui::GetWindowSize().y / 2.0f - fitTextureSize.y * zoom / 2
            } + imageOffset);
            ImGui::Image(texture.handle, fitTextureSize * zoom);

            auto footerText = FormatString("%ix%i; %s", (int) texture.width, (int) texture.height, texture.PrecisionToString().c_str());
            ImVec2 footerSize = ImGui::CalcTextSize(footerText.c_str());
            ImGui::SetCursorPos({
                ImGui::GetWindowSize().x / 2.0f - footerSize.x / 2.0f,
                ImGui::GetWindowSize().y - footerSize.y
            });
            ImGui::Text(footerText.c_str());

            imageDrag.Activate();
            float imageDragDistance;
            if (imageDrag.GetDragDistance(imageDragDistance)) {
                imageOffset = imageOffset + ImGui::GetIO().MouseDelta;
            } else imageDrag.Deactivate();
            if (ImGui::GetIO().MouseWheel != 0 && ImGui::IsWindowFocused()) {
                zoom += ImGui::GetIO().MouseWheel * 0.1f;
                zoom = std::max(zoom, 0.5f);
            }
        ImGui::EndChild();
    }

    void PreviewDispatchers::DispatchFloatValue(std::any& t_attribute) {
        static ImVec2 plotSize = ImVec2(200, 40);

        ImGui::SetCursorPos({
            ImGui::GetWindowSize().x / 2.0f - plotSize.x / 2.0f,
            ImGui::GetWindowSize().y / 2.0f - plotSize.y / 2.0f
        });
        float value = std::any_cast<float>(t_attribute);
        ImGui::PlotVar("", value);
    }

    void PreviewDispatchers::DispatchVector4Value(std::any& t_attribute) {
        auto vector = std::any_cast<glm::vec4>(t_attribute);
        static bool interpretAsColor = false;
        static float interpreterModeSizeX = 0;
        float beginCursorX = ImGui::GetCursorPosX();

        ImVec4 buttonColor = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        ImVec4 reservedButtonColor = buttonColor;

        if (!interpretAsColor) buttonColor = buttonColor * 1.2f;
        buttonColor.w = 1.0f;

        ImGui::SetCursorPosX(ImGui::GetWindowSize().x / 2.0f - interpreterModeSizeX / 2.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        if (ImGui::Button(FormatString("%s %s", interpretAsColor ? ICON_FA_EXPAND : ICON_FA_CHECK, Localization::GetString("VECTOR").c_str()).c_str())) {
            interpretAsColor = false;
        }
        buttonColor = reservedButtonColor;

        ImGui::SameLine();

        if (interpretAsColor) buttonColor = buttonColor * 1.2f;
        buttonColor.w = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        if (ImGui::Button(FormatString("%s %s", interpretAsColor ? ICON_FA_CHECK : ICON_FA_DROPLET, Localization::GetString("COLOR").c_str()).c_str())) {
            interpretAsColor = true;
        }
        buttonColor = reservedButtonColor;

        ImGui::PopStyleColor(2);
        ImGui::SameLine();


        interpreterModeSizeX = ImGui::GetCursorPosX() - beginCursorX;

        ImGui::NewLine();

        static ImVec2 childSize = ImVec2(100, 100);
        ImGui::SetCursorPos(ImVec2{
            ImGui::GetWindowSize().x / 2.0f - childSize.x / 2.0f,
            ImGui::GetWindowSize().y / 2.0f - childSize.y / 2.0f
        });

        ImGui::BeginChild("##vector4Container", ImVec2(0, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
        if (!interpretAsColor) {
            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1, 0, 0, 1));
            ImGui::PlotVar(FormatString("%s %s", ICON_FA_STOPWATCH, "(x)").c_str(), vector.x);

            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0, 1, 0, 1));
            ImGui::PlotVar(FormatString("%s %s", ICON_FA_STOPWATCH, "(y)").c_str(), vector.y);

            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0, 0, 1, 1));
            ImGui::PlotVar(FormatString("%s %s", ICON_FA_STOPWATCH, "(z)").c_str(), vector.z);

            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1, 1, 0, 1));
            ImGui::PlotVar(FormatString("%s %s", ICON_FA_STOPWATCH, "(w)").c_str(), vector.w);
            ImGui::PopStyleColor(4);
        } else {
            float vectorPtr[4] = {
                vector.x,
                vector.y,
                vector.z,
                vector.w
            };
            ImGui::PushItemWidth(200);
                ImGui::ColorPicker4("##colorPreview", vectorPtr, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
            ImGui::PopItemWidth();
        }
        childSize = ImGui::GetWindowSize();
        ImGui::EndChild();
    }
};
