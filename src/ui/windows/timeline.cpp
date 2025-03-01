#include "timeline.h"
#include "font/font.h"
#include "common/attributes.h"
#include "common/ui_shared.h"
#include "compositor/compositor.h"
#include "compositor/blending.h"
#include "common/transform2d.h"
#include "common/dispatchers.h"

#define SPLITTER_RULLER_WIDTH 8
#define TIMELINE_RULER_WIDTH 4
#define TICKS_BAR_HEIGHT 30
#define TICK_SMALL_WIDTH 3
#define LAYER_HEIGHT 44
#define LAYER_SEPARATOR 1

#define ROUND_EVEN(x) std::round( (x) * 0.5f ) * 2.0f

#define LAYER_REARRANGE_DRAG_DROP "LAYER_REARRANGE_DRAG_DROP"

namespace Raster {

    enum class TimelineChannels {
        Compositions,
        Separators,
        Timestamps,
        TimelineRuler,
        Count
    };

    enum class TimestampFormat {
        Regular, Frame, Seconds
    };

    static void SplitDrawList() {
        ImGui::GetWindowDrawList()->ChannelsSplit(static_cast<int>(TimelineChannels::Count));
    }

    static void SetDrawListChannel(TimelineChannels channel) {
        ImGui::GetWindowDrawList()->ChannelsSetCurrent(static_cast<int>(channel));
    }

    static glm::vec2 FitRectInRect(glm::vec2 dst, glm::vec2 src) {
        float scale = std::min(dst.x / src.x, dst.y / src.y);
        return glm::vec2{src.x * scale, src.y * scale};
    }

    static float s_splitterState = 0.3f;
    static DragStructure s_dragStructure;

    static std::vector<float> s_layerSeparators;

    static std::unordered_map<int, float> s_attributeYCursors;
    static std::unordered_map<int, bool> s_attributesExpanded;
    static std::unordered_map<int, ImGuiID> s_compositionTrees;
    static std::unordered_map<int, float> s_compositionTreeScrolls;

    static float s_targetLegendScroll = -1;

    static float s_pixelsPerFrame = 4;

    static bool s_timelineRulerDragged = false;
    static bool s_anyLayerDragged = false;
    static bool s_scrollbarActive = true;
    static bool s_layerPopupActive = false;
    static bool s_anyCompositionWasPressed = false;
    static bool s_timelineFocused = false;

    static float s_timelineScrollY = 0;

    static float s_compositionsEditorCursorX = 0;

    static DragStructure s_timelineDrag;

    static std::unordered_map<int, float> s_legendOffsets;
    static ImGuiID s_legendTargetOpenTree = 0;

    static ImVec2 s_rootWindowSize;

    static std::vector<Composition> s_copyCompositions;

    static std::string s_compositionFilter = "";
    static uint32_t s_colorMarkFilter = IM_COL32(0, 0, 0, 0);

    static float precision(float f, int places) {
        float n = std::pow(10.0f, places ) ;
        return std::round(f * n) / n ;
    }

    static void DrawRect(RectBounds bounds, ImVec4 color) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            bounds.UL, bounds.BR, ImGui::ColorConvertFloat4ToU32(color));
    }

    static void PushClipRect(RectBounds bounds) {
        ImGui::GetWindowDrawList()->PushClipRect(bounds.UL, bounds.BR, true);
    }

    static void PopClipRect() { ImGui::GetWindowDrawList()->PopClipRect(); }

    static bool MouseHoveringBounds(RectBounds bounds) {
        return ImGui::IsMouseHoveringRect(bounds.UL, bounds.BR);
    }

    static bool ClampedButton(const char* label, const ImVec2& size_arg, ImGuiButtonFlags flags, bool& hovered, Composition* t_composition = nullptr)
    {
        ImVec2 baseCursor = ImGui::GetCursorPos();
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

        ImVec2 pos = window->DC.CursorPos;
        if ((flags & ImGuiButtonFlags_AlignTextBaseLine) && style.FramePadding.y < window->DC.CurrLineTextBaseOffset) // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
            pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
        ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

        const ImRect bb(pos, pos + size);
        ImGui::ItemSize(size, style.FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

        // Render
        const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        ImGui::RenderNavHighlight(bb, id);
        ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

        if (t_composition) {
            TimelineUI::RenderLayerDragDrop(t_composition);
        }

        if (g.LogEnabled)
            ImGui::LogSetNextTextDecoration("[", "]");

        ImVec2 textMin = bb.Min + style.FramePadding;
        ImVec2 textMax = bb.Max - style.FramePadding;

        ImVec2 reservedCursor = ImGui::GetCursorPos();

        ImVec2 labelCursor = {
            baseCursor.x + size.x / 2.0f - label_size.x / 2.0f,
            baseCursor.y + size.y / 2.0f - label_size.y / 2.0f
        };
        if (labelCursor.x - ImGui::GetScrollX() <= style.FramePadding.x) {
            labelCursor.x = ImGui::GetScrollX() + style.FramePadding.x;
        }
        if (labelCursor.x - ImGui::GetScrollX() >= ImGui::GetWindowSize().x - style.FramePadding.x - label_size.x) {
            labelCursor.x = ImGui::GetScrollX() + ImGui::GetWindowSize().x - style.FramePadding.x - label_size.x;
        }

        ImGui::PushClipRect(bb.Min, bb.Max, true);
        ImGui::SetCursorPos(labelCursor);
        ImGui::Text("%s", label);
        ImGui::SetCursorPos(reservedCursor);
        ImGui::PopClipRect();

        // Automatically close popups
        //if (pressed && !(flags & ImGuiButtonFlags_DontClosePopups) && (window->Flags & ImGuiWindowFlags_Popup))
        //    CloseCurrentPopup();

        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
        return pressed;
    }

    static bool TextColorButton(const char* id, ImVec4 color) {
        if (ImGui::BeginChild(FormatString("##%scolorMark", id).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0), ImGuiChildFlags_AutoResizeY)) {
            ImGui::SetCursorPos({0, 0});
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color * 1.1f);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, color * 1.2f);
            ImGui::ColorButton(FormatString("%s %s", ICON_FA_DROPLET, id).c_str(), color, ImGuiColorEditFlags_AlphaPreview);
            ImGui::PopStyleColor(3);
            ImGui::SameLine();
            if (ImGui::IsWindowHovered()) ImGui::BeginDisabled();
            std::string defaultColorMarkText = "";
            if (Workspace::s_defaultColorMark == id) {
                defaultColorMarkText = FormatString(" (%s)", Localization::GetString("DEFAULT").c_str());
            }
            ImGui::Text("%s %s%s", ICON_FA_DROPLET, id, defaultColorMarkText.c_str());
            if (ImGui::IsWindowHovered()) ImGui::EndDisabled();
        }
        ImGui::EndChild();
        return ImGui::IsItemClicked();
    }

    void TimelineUI::Render() {
        PushStyleVars();

        s_layerPopupActive = false;
        s_anyCompositionWasPressed = false;

        UIShared::s_timelinePixelsPerFrame = s_pixelsPerFrame;
        ImGui::Begin(FormatString("%s %s", ICON_FA_TIMELINE, Localization::GetString("TIMELINE").c_str()).c_str());
            if (!Workspace::IsProjectLoaded()) {
                ImGui::PushFont(Font::s_denseFont);
                ImGui::SetWindowFontScale(2.0f);
                    ImVec2 exclamationSize = ImGui::CalcTextSize(ICON_FA_TRIANGLE_EXCLAMATION);
                    ImGui::SetCursorPos(ImGui::GetWindowSize() / 2.0f - exclamationSize / 2.0f);
                    ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopFont();
                ImGui::End();
                PopStyleVars();
                return;
            }
            auto& project = Workspace::GetProject();
            if (project.customData.contains("TimelineColorFilter")) {
                s_colorMarkFilter = project.customData["TimelineColorFilter"];
            }
            if (project.customData.contains("TimelineSplitterState")) {
                s_splitterState = project.customData["TimelineSplitterState"];
            }
            s_rootWindowSize = ImGui::GetWindowSize();
            s_timelineFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)  && ImGui::GetIO().KeyCtrl) {
                s_pixelsPerFrame += 1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) && ImGui::GetIO().KeyCtrl) {
                s_pixelsPerFrame -= 1;
            }
            s_pixelsPerFrame = std::clamp((int) s_pixelsPerFrame, 1, 10);
            UIShared::s_timelineDragged = s_timelineRulerDragged;

            RenderLegend();
            RenderCompositionsEditor();
            RenderSplitter();
            s_legendOffsets.clear();
            project.customData["TimelineColorFilter"] = s_colorMarkFilter;
            project.customData["TimelineSplitterState"] = s_splitterState;
        ImGui::End();
        PopStyleVars();
    }

    void TimelineUI::PushStyleVars() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    }


    void TimelineUI::PopStyleVars() {
        ImGui::PopStyleVar(2);
    }

    void TimelineUI::RenderTicksBar() {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        RectBounds backgroundBounds = RectBounds(
            ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY()),
            ImVec2(ImGui::GetWindowSize().x, TICKS_BAR_HEIGHT)
        );

        DrawRect(backgroundBounds, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        if (MouseHoveringBounds(backgroundBounds) && !s_anyLayerDragged) {
            s_timelineDrag.Activate();
        }

        SetDrawListChannel(TimelineChannels::Separators);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(
            backgroundBounds.UL.x, backgroundBounds.BR.y
        ), backgroundBounds.BR, IM_COL32(0, 0, 0, 255), 1.0f);
        SetDrawListChannel(TimelineChannels::Compositions);
    }

    void TimelineUI::RenderTicks() {
        RenderTicksBar();

        RectBounds backgroundBounds = RectBounds(
            ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY()),
            ImVec2(ImGui::GetWindowSize().x, TICKS_BAR_HEIGHT)
        );


        auto& project = Workspace::s_project.value();

        int desiredTicksCount = ROUND_EVEN(5 * s_pixelsPerFrame / 2);
        int tickStep =
            project.framerate / desiredTicksCount;
        int tickPositionStep = tickStep * s_pixelsPerFrame;
        int tickPositionAccumulator = 0;
        int previousTickPositionAccumulator = 0;
        int tickAccumulator = 0;
        int ticksMajorAccumulator = 0;
        while (tickAccumulator <= project.GetProjectLength()) {
            bool majorTick = remainder((int) tickAccumulator,
                                    (int) project.framerate) == 0.0f;
            bool mediumTick = remainder(tickAccumulator, project.framerate / 2) == 0;
            float tickHeight = majorTick ? ImGui::GetWindowSize().y : TICKS_BAR_HEIGHT / 4;
            if (!majorTick && mediumTick) {
                tickHeight = TICKS_BAR_HEIGHT / 2;
            }
            RectBounds tickBounds = RectBounds(
                ImVec2(tickPositionAccumulator, 0 + ImGui::GetScrollY()),
                ImVec2(TICK_SMALL_WIDTH, tickHeight));
            
            std::string timestampFormattedText = project.FormatFrameToTime(tickAccumulator);
            ImVec2 timestampSize = ImGui::CalcTextSize(timestampFormattedText.c_str());

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            SetDrawListChannel(TimelineChannels::Compositions);
            DrawRect(tickBounds, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

            ticksMajorAccumulator++;

            if (majorTick) {
                SetDrawListChannel(TimelineChannels::Timestamps);
                drawList->AddText(
                    ImGui::GetCursorScreenPos() + ImVec2(tickBounds.pos.x + 5, 6.0f),
                    IM_COL32(255, 255, 255, 255), timestampFormattedText.c_str()
                );
                previousTickPositionAccumulator = tickPositionAccumulator;
                ticksMajorAccumulator = 0;
            }
            SetDrawListChannel(TimelineChannels::Compositions);

            tickPositionAccumulator += tickPositionStep;
            tickAccumulator += tickStep;
        }
    }

    void TimelineUI::ProcessShortcuts() {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C) && UIShared::s_lastClickedObjectType == LastClickedObjectType::Composition) {
            ProcessCopyAction();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V) && UIShared::s_lastClickedObjectType == LastClickedObjectType::Composition) {
            ProcessPasteAction();
        }
        if (ImGui::Shortcut(ImGuiKey_Delete) && UIShared::s_lastClickedObjectType == LastClickedObjectType::Composition) {
            ProcessDeleteAction();
        }
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D) && UIShared::s_lastClickedObjectType == LastClickedObjectType::Composition) {
            ProcessCopyAction();
            ProcessPasteAction();
        }
    }

    void TimelineUI::RenderCompositionsEditor() {
        ImGui::SameLine();
        s_compositionsEditorCursorX = ImGui::GetCursorPosX();

        auto& project = Workspace::s_project.value();

        RectBounds backgroundBounds = RectBounds(
            ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY()),
            ImVec2(ImGui::GetWindowSize().x, TICKS_BAR_HEIGHT)
        );

        ImVec2 windowSize = ImGui::GetWindowSize();
        static bool showHorizontalScrollbar = false;
        static bool showVerticalScrollbar = false;
        ImGuiWindowFlags timelineFlags = 0;
        if (showHorizontalScrollbar)
            timelineFlags |= ImGuiWindowFlags_AlwaysHorizontalScrollbar;
        if (showVerticalScrollbar)
            timelineFlags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
        ImVec4 editorBackgroundColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) * 1.2f;
        editorBackgroundColor.w = 1.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, editorBackgroundColor);
        bool timelineCompositionsChildStatus = ImGui::BeginChild("##timelineCompositions", ImVec2(ImGui::GetWindowSize().x * (1 - s_splitterState), ImGui::GetContentRegionAvail().y), 0, timelineFlags);
        ImGui::PopStyleColor();
        if (timelineCompositionsChildStatus) {
            SplitDrawList();
            AttributeBase::ProcessKeyframeShortcuts();

            s_timelineScrollY = ImGui::GetScrollY();
            ImVec2 windowMouseCoords = GetRelativeMousePos();
            bool previousVerticalBar = showVerticalScrollbar;
            bool previousHorizontalBar = showHorizontalScrollbar;
            showVerticalScrollbar = (windowMouseCoords.x - ImGui::GetScrollX() >= (ImGui::GetWindowSize().x - 20)) && windowMouseCoords.x - ImGui::GetScrollY() < ImGui::GetWindowSize().x + 5;
            showHorizontalScrollbar = (windowMouseCoords.y - ImGui::GetScrollY() >= (ImGui::GetWindowSize().y - 20)) && windowMouseCoords.y - ImGui::GetScrollY() < ImGui::GetWindowSize().y + 5;
            if (previousVerticalBar && ImGui::GetIO().MouseDown[ImGuiMouseButton_Left])
                showVerticalScrollbar = true;
            if (previousHorizontalBar && ImGui::GetIO().MouseDown[ImGuiMouseButton_Left])
                showHorizontalScrollbar = true;

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            RenderTicks();

            float layerAccumulator = 0;
            s_anyLayerDragged = false;
            for (int i = project.compositions.size(); i --> 0;) {
                auto& composition = project.compositions[i];
                if (!s_compositionFilter.empty() && LowerCase(composition.name).find(LowerCase(s_compositionFilter)) == std::string::npos) continue;
                if (s_colorMarkFilter != IM_COL32(0, 0, 0, 0) && composition.colorMark != s_colorMarkFilter) continue;
                ImGui::SetCursorPosY(backgroundBounds.size.y + layerAccumulator);
                RenderComposition(composition.id);
                float legendOffset = 0;
                if (s_legendOffsets.find(composition.id) != s_legendOffsets.end()) {
                    legendOffset = s_legendOffsets[composition.id];
                }
                layerAccumulator += LAYER_HEIGHT + legendOffset;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && project.selectedCompositions.size() > 1 && !s_anyCompositionWasPressed && ImGui::IsWindowFocused()) {
                project.selectedCompositions = {project.selectedCompositions[0]};
                UIShared::s_lastClickedObjectType = LastClickedObjectType::Composition;
                std::cout << "overriding compositions" << std::endl;
            }
            
            RenderTimelinePopup();

            ImGui::SetCursorPos({0, 0});
            RectBounds shadowGradientBounds(
                ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY()), 
                ImVec2(30, ImGui::GetWindowSize().x)
            );
            int shadowAlpha = 128;
            ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
                shadowGradientBounds.UL, shadowGradientBounds.BR,
                IM_COL32(0, 0, 0, shadowAlpha), IM_COL32(0, 0, 0, 0),
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, shadowAlpha) 
            );
            RenderTimelineRuler();
        }
        
        ProcessShortcuts();
        ImGui::EndChild();
    }

    void TimelineUI::RenderComposition(int t_id) {
        auto compositionCandidate = Workspace::GetCompositionByID(t_id);
        auto& project = Workspace::s_project.value();
        if (compositionCandidate.has_value()) {
            auto& composition = compositionCandidate.value();
            auto& selectedComposoitions = project.selectedCompositions;
            ImGui::PushID(composition->id);
            ImGui::SetCursorPosX(std::ceil(composition->beginFrame * s_pixelsPerFrame));
            ImVec4 buttonColor = ImGui::ColorConvertU32ToFloat4(composition->colorMark);
            bool isCompositionSelected = false;
            if (std::find(selectedComposoitions.begin(), selectedComposoitions.end(), t_id) != selectedComposoitions.end()) {
                buttonColor = 1.1f * buttonColor;
                isCompositionSelected = true;
            }
            if (!composition->enabled) buttonColor = buttonColor * 0.8f;
            buttonColor.w = 1.0f;
            PopStyleVars();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
            ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonColor * 1.1f);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonColor * 1.2f);
            ImVec2 buttonCursor = ImGui::GetCursorPos();
            ImVec2 buttonSize = ImVec2(std::ceil((composition->endFrame - composition->beginFrame) * s_pixelsPerFrame), LAYER_HEIGHT);
            bool compositionHovered;
            bool compositionPressed = ClampedButton(FormatString("%s %s", ICON_FA_LAYER_GROUP, composition->name.c_str()).c_str(), buttonSize, 0, compositionHovered, composition);
            if (compositionHovered) {
                s_anyCompositionWasPressed = true;
            }
            bool mustDeleteComposition = false;

            static std::vector<DragStructure> s_layerDrags, s_forwardBoundsDrags, s_backwardBoundsDrags;
            s_layerDrags.resize(project.compositions.size());
            s_forwardBoundsDrags.resize(project.compositions.size());
            s_backwardBoundsDrags.resize(project.compositions.size());

            int compositionIndex = 0;
            for (auto& compositionIterable : project.compositions) {
                if (compositionIterable.id == t_id) break;
                compositionIndex++;
            }

            DragStructure& s_layerDrag = s_layerDrags[compositionIndex];
            DragStructure& s_forwardBoundsDrag = s_forwardBoundsDrags[compositionIndex];
            DragStructure& s_backwardBoundsDrag = s_backwardBoundsDrags[compositionIndex];

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            PushStyleVars();
            auto& io = ImGui::GetIO();
            // auto& project = Workspace::GetProject();

            if (compositionPressed && !io.KeyCtrl && std::abs(io.MouseDelta.x) < 0.1f) {
                project.selectedCompositions = {composition->id};
                if (!composition->attributes.empty()) {
                    for (auto& attribute : composition->attributes) {
                        if (attribute->Get(project.currentFrame - composition->beginFrame, composition).type() == typeid(Transform2D)) {
                            project.selectedAttributes = {attribute->id};
                        }
                    }
                }
            }

            if (ImGui::GetIO().KeyCtrl && compositionHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (!isCompositionSelected) {
                    AppendSelectedCompositions(composition);
                } else if (!s_layerDrag.isActive && !s_forwardBoundsDrag.isActive && !s_backwardBoundsDrag.isActive) {
                    mustDeleteComposition = true;
                }
                UIShared::s_lastClickedObjectType = LastClickedObjectType::Composition;
            }

            ImVec2 dragSize = ImVec2((composition->endFrame - composition->beginFrame) * s_pixelsPerFrame / 10, LAYER_HEIGHT - 1);
            dragSize.x = std::clamp(dragSize.x, 1.0f, 30.0f);

            ImGui::SetCursorPos({0, 0});
            RectBounds forwardBoundsDrag(
                ImVec2(buttonCursor.x + buttonSize.x - dragSize.x, buttonCursor.y + 1),
                dragSize
            );

            RectBounds backwardBoundsDrag(
                ImVec2(buttonCursor.x, buttonCursor.y + 1), dragSize
            );

            ImVec4 dragColor = buttonColor * 0.7f;
            dragColor.w = 1.0f;

            ImVec4 forwardDragColor = dragColor;
            if (MouseHoveringBounds(forwardBoundsDrag) && s_timelineFocused) {
                forwardDragColor = forwardDragColor * 1.1f;
                if (ImGui::GetIO().MouseDown[ImGuiMouseButton_Left]) {
                    forwardDragColor = forwardDragColor * 1.1f;
                }
            }

            ImVec4 backwardDragColor = dragColor;
            if (MouseHoveringBounds(backwardBoundsDrag) && s_timelineFocused) {
                backwardDragColor = backwardDragColor * 1.1f;
                if (ImGui::GetIO().MouseDown[ImGuiMouseButton_Left]) {
                    backwardDragColor = backwardDragColor * 1.1f;
                }
            }

            forwardDragColor.w = 1.0f;


            DrawRect(forwardBoundsDrag, forwardDragColor);
            DrawRect(backwardBoundsDrag, backwardDragColor);

            auto& bundles = Compositor::s_bundles;
            if (bundles.find(t_id) != bundles.end()) {
                auto& bundle = bundles[t_id];
                if (bundle.primaryFramebuffer.handle) {
                    auto& framebuffer = bundle.primaryFramebuffer;
                    glm::vec2 previewSize = {LAYER_HEIGHT * ((float) framebuffer.width / (float) framebuffer.height), LAYER_HEIGHT};
                    glm::vec2 rectSize = FitRectInRect(previewSize, glm::vec2{(float) framebuffer.width, (float) framebuffer.height});
                    float legendWidth = s_splitterState * s_rootWindowSize.x;
                    ImVec2 upperLeft = ImGui::GetCursorScreenPos() + buttonCursor;
                    ImVec2 bottomRight = upperLeft;
                    bottomRight += ImVec2{rectSize.x, rectSize.y};
                    upperLeft.x += dragSize.x;
                    bottomRight.x += dragSize.x;
                    upperLeft.x = glm::max(upperLeft.x, legendWidth);
                    bottomRight.x = glm::max(bottomRight.x, legendWidth + rectSize.x);
                    bottomRight.x = glm::min(bottomRight.x, ImGui::GetCursorScreenPos().x + buttonCursor.x + buttonSize.x - dragSize.x);
                    auto reservedCursor = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(buttonCursor);
                    ImGui::SetCursorPosX(buttonCursor.x + dragSize.x);
                    ImGui::Stripes(ImVec4(0.05f, 0.05f, 0.05f, 0.8f), ImVec4(0.1f, 0.1f, 0.1f, 0.8f), 14, 214, ImVec2{rectSize.x, rectSize.y});
                    ImGui::GetWindowDrawList()->AddImage((ImTextureID) framebuffer.attachments[0].handle, upperLeft, bottomRight);
                    if (ImGui::IsMouseHoveringRect(upperLeft, bottomRight) && s_timelineFocused) {
                        PopStyleVars();
                        if (ImGui::BeginTooltip()) {
                            std::any dynamicFramebuffer = framebuffer;
                            Dispatchers::DispatchString(dynamicFramebuffer);
                            ImGui::EndTooltip();
                        }
                        PushStyleVars();
                    }
                    ImGui::SetCursorPos(reservedCursor);
                }
            }

            if ((MouseHoveringBounds(forwardBoundsDrag) || s_forwardBoundsDrag.isActive) && !s_timelineRulerDragged && !s_layerDrag.isActive && !s_backwardBoundsDrag.isActive && !UIShared::s_timelineAnykeyframeDragged) {
                s_forwardBoundsDrag.Activate();
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                for (auto& selectedComposoitionID : selectedComposoitions) {
                    auto selectedCompositionCandidate = Workspace::GetCompositionByID(selectedComposoitionID);
                    if (selectedCompositionCandidate.has_value()) {
                        auto& selectedComposition = selectedCompositionCandidate.value();

                        float boundsDragDistance;
                        if (s_forwardBoundsDrag.GetDragDistance(boundsDragDistance)) {
                            selectedComposition->endFrame += boundsDragDistance / s_pixelsPerFrame;
                        } else s_forwardBoundsDrag.Deactivate();

                        float scrollAmount = ProcessLayerScroll();
                        selectedComposition->endFrame += scrollAmount / s_pixelsPerFrame;
                    }
                }
            }

            if ((MouseHoveringBounds(backwardBoundsDrag) || s_backwardBoundsDrag.isActive) && !s_timelineRulerDragged && !s_layerDrag.isActive && !s_forwardBoundsDrag.isActive && !UIShared::s_timelineAnykeyframeDragged) {
                s_backwardBoundsDrag.Activate();
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

                for (auto& selectedComposoitionID : selectedComposoitions) {
                    auto selectedCompositionCandidate = Workspace::GetCompositionByID(selectedComposoitionID);
                    if (selectedCompositionCandidate.has_value()) {
                        auto& selectedComposition = selectedCompositionCandidate.value();
                        float boundsDragDistance;
                        if (s_backwardBoundsDrag.GetDragDistance(boundsDragDistance)) {
                            selectedComposition->beginFrame += boundsDragDistance / s_pixelsPerFrame;
                        } else s_backwardBoundsDrag.Deactivate();

                        float scrollAmount = ProcessLayerScroll();
                        selectedComposition->beginFrame += scrollAmount / s_pixelsPerFrame;
                    }
                }
            }
 
            if ((compositionHovered || s_layerDrag.isActive) && !s_timelineRulerDragged && !s_backwardBoundsDrag.isActive && !s_forwardBoundsDrag.isActive && !UIShared::s_timelineAnykeyframeDragged) {
                s_layerDrag.Activate();

                float layerDragDistance;
                if (s_layerDrag.GetDragDistance(layerDragDistance) && !s_timelineRulerDragged) {
                    for (auto& selectedComposoitionID : selectedComposoitions) {
                        bool breakDrag = false;
                        for (auto& testingCompositionID : selectedComposoitions) {
                            auto selectedCompositionCandidate = Workspace::GetCompositionByID(testingCompositionID);
                            if (selectedCompositionCandidate.has_value()) {
                                auto& testingComposition = selectedCompositionCandidate.value();
                                if (testingComposition->beginFrame <= 0) {
                                    breakDrag = true;
                                    break;
                                }
                            }
                        }
                        if (breakDrag && selectedComposoitions.size() > 1 && layerDragDistance < 0) break;
                        auto selectedCompositionCandidate = Workspace::GetCompositionByID(selectedComposoitionID);
                        if (selectedCompositionCandidate.has_value() && ImGui::IsWindowFocused()) {
                            auto& selectedComposition = selectedCompositionCandidate.value();
                            ImVec2 reservedBounds = ImVec2(selectedComposition->beginFrame, selectedComposition->endFrame);

                            selectedComposition->beginFrame += layerDragDistance / s_pixelsPerFrame;
                            selectedComposition->endFrame += layerDragDistance / s_pixelsPerFrame;

                            float scrollAmount = ProcessLayerScroll();
                            selectedComposition->beginFrame += scrollAmount / s_pixelsPerFrame;
                            selectedComposition->endFrame += scrollAmount / s_pixelsPerFrame;

                            if (selectedComposition->beginFrame < 0) {
                                selectedComposition->beginFrame = reservedBounds.x;
                                selectedComposition->endFrame = reservedBounds.y;
                            }

                            selectedComposition->beginFrame = std::max(selectedComposition->beginFrame, 0.0f);
                            selectedComposition->endFrame = std::max(selectedComposition->endFrame, 0.0f);
                        }
                    }
                    
                } else s_layerDrag.Deactivate();
            }

            s_anyLayerDragged = s_anyLayerDragged || s_layerDrag.isActive || s_backwardBoundsDrag.isActive || s_forwardBoundsDrag.isActive;

            if (compositionHovered && ImGui::GetIO().MouseDoubleClicked[ImGuiMouseButton_Left]) {
                ImGui::OpenPopup(FormatString("##renameComposition%i", t_id).c_str());
            }
            static bool renameFieldFocued = false;
            PopStyleVars();
            if (ImGui::BeginPopup(FormatString("##renameComposition%i", t_id).c_str())) {
                if (!renameFieldFocued) {
                    ImGui::SetKeyboardFocusHere(0);
                    renameFieldFocued = true;
                }
                ImGui::InputTextWithHint("##renameField", FormatString("%s %s", ICON_FA_PENCIL, Localization::GetString("COMPOSITION_NAME").c_str()).c_str(), &composition->name);
                if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            } else renameFieldFocued = false;
            PushStyleVars();

            if (compositionHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup(FormatString("##compositionPopup%i", composition->id).c_str());
            }

            PopStyleVars();
            if (ImGui::BeginPopup(FormatString("##compositionPopup%i", composition->id).c_str())) {
                RenderCompositionPopup(composition);
                ImGui::EndPopup();
                s_layerPopupActive = true;
            } else if (!s_layerPopupActive) {
                s_layerPopupActive = false;
            }
            PushStyleVars();
            ImGui::PopID();

            SetDrawListChannel(TimelineChannels::Separators);
            ImGui::SetCursorPosX(0);
            RectBounds separatorBounds(
                ImVec2(ImGui::GetScrollX(), buttonCursor.y + buttonSize.y - LAYER_SEPARATOR / 2.0f),
                ImVec2(ImGui::GetWindowSize().x, LAYER_SEPARATOR)
            );
            DrawRect(separatorBounds, ImVec4(0, 0, 0, 1));
            SetDrawListChannel(TimelineChannels::Compositions);

            for (auto& attribute : composition->attributes) {
                ImGui::SetCursorPosY(s_attributeYCursors[attribute->id]);
                PopStyleVars();
                if (s_attributesExpanded.find(t_id) != s_attributesExpanded.end() && s_attributesExpanded[t_id]) {
                    attribute->RenderKeyframes();
                }
                PushStyleVars();
            }

            if (mustDeleteComposition) {
                int compositionIndex = 0;
                for (auto& selectedComposoition : selectedComposoitions) {
                    if (selectedComposoition == t_id) break;
                    compositionIndex++;
                }
                selectedComposoitions.erase(selectedComposoitions.begin() + compositionIndex);
            }
        }
    }

    void TimelineUI::DeleteComposition(Composition* composition) {
        auto& project = Workspace::s_project.value();
        
        auto selectedCompositionIterator = std::find(project.selectedCompositions.begin(), project.selectedCompositions.end(), composition->id);
        if (selectedCompositionIterator != project.selectedCompositions.end()) {
            project.selectedCompositions.erase(selectedCompositionIterator);
        }

        int targetCompositionIndex = 0;
        for (auto& iterationComposition : project.compositions) {
            if (composition->id == iterationComposition.id) break;
            targetCompositionIndex++;
        }
        project.compositions.erase(project.compositions.begin() + targetCompositionIndex);
    }

    void TimelineUI::AppendSelectedCompositions(Composition* composition) {
        auto& project = Workspace::GetProject();
        auto& selectedCompositions = project.selectedCompositions;
        if (std::find(selectedCompositions.begin(), selectedCompositions.end(), composition->id) == selectedCompositions.end()) {
            selectedCompositions.push_back(composition->id);
        }
    }

    void TimelineUI::RenderCompositionPopup(Composition* t_composition, ImGuiID t_parentTreeID) {
        auto& project = Workspace::GetProject();
        auto& selectedCompositions = project.selectedCompositions;
        ImGui::SeparatorText(FormatString("%s %s", ICON_FA_LAYER_GROUP, t_composition->name.c_str()).c_str());
        if (ImGui::BeginMenu(FormatString("%s %s", ICON_FA_PLUS, Localization::GetString("ADD_ATTRIBUTE").c_str()).c_str())) {
            RenderNewAttributePopup(t_composition, t_parentTreeID);
            ImGui::EndMenu();
        }
        std::string blendingPreviewText = "";
        auto modeCandidate = Compositor::s_blending.GetModeByCodeName(t_composition->blendMode);
        if (modeCandidate.has_value()) {
            blendingPreviewText = "(" + modeCandidate.value().name + ")";
        }
        if (ImGui::BeginMenu(FormatString("%s %s %s", ICON_FA_SLIDERS, Localization::GetString("BLENDING").c_str(), blendingPreviewText.c_str()).c_str())) {
            ImGui::SeparatorText(FormatString("%s %s", ICON_FA_SLIDERS, Localization::GetString("BLENDING").c_str()).c_str());
            static std::string blendFilter = "";
            bool attributeOpacityUsed;
            bool correctOpacityTypeUsed;
            float opacity = t_composition->GetOpacity(&attributeOpacityUsed, &correctOpacityTypeUsed);
            std::string attributeSelectorText = ICON_FA_LINK;
            if (attributeOpacityUsed) {
                if (correctOpacityTypeUsed) attributeSelectorText = ICON_FA_CHECK " " ICON_FA_LINK;
                else attributeSelectorText = ICON_FA_TRIANGLE_EXCLAMATION " " ICON_FA_LINK;
            }
            static float searchBarWidth = 30;
            static ImVec2 fullWindowSize = ImGui::GetWindowSize();
            ImGui::BeginChild("##opacityContainer", ImVec2(fullWindowSize.x, 0), ImGuiChildFlags_AutoResizeY);
            if (ImGui::Button(attributeSelectorText.c_str())) {
                ImGui::OpenPopup("##opacityAttributeChooser");
            } 
            ImGui::SetItemTooltip("%s %s", ICON_FA_DROPLET, Localization::GetString("OPACITY_ATTRIBUTE").c_str());
            if (attributeOpacityUsed && !correctOpacityTypeUsed) {
                ImGui::SetItemTooltip("%s %s", ICON_FA_TRIANGLE_EXCLAMATION, Localization::GetString("BAD_OPACITY_ATTRIBUTE").c_str());
            } else {
                ImGui::SetItemTooltip("%s %s", ICON_FA_LINK, Localization::GetString("OPACITY_ATTRIBUTE").c_str());
            }
            if (ImGui::BeginPopup("##opacityAttributeChooser")) {
                ImGui::SeparatorText(FormatString("%s %s", ICON_FA_LINK, Localization::GetString("OPACITY_ATTRIBUTE").c_str()).c_str());
                static std::string attributeFilter = "";
                ImGui::InputTextWithHint("##attributeSearchFilter", FormatString("%s %s", ICON_FA_MAGNIFYING_GLASS, Localization::GetString("SEARCH_FILTER").c_str()).c_str(), &attributeFilter);
                if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_XMARK, Localization::GetString("NO_ATTRIBUTE").c_str()).c_str())) {
                    t_composition->opacityAttributeID = -1;
                }
                for (auto& attribute : t_composition->attributes) {
                    if (!attributeFilter.empty() && attribute->name.find(attributeFilter) == std::string::npos) continue;
                    ImGui::PushID(attribute->id);
                        if (ImGui::MenuItem(FormatString("%s%s %s", t_composition->opacityAttributeID == attribute->id ? ICON_FA_CHECK " " : "", ICON_FA_LINK, attribute->name.c_str()).c_str())) {
                            t_composition->opacityAttributeID = attribute->id;
                        } 
                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine(0, 2.0f);
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderFloat("##compositionOpacity", &opacity, 0, 1);
            ImGui::SetItemTooltip("%s %s", ICON_FA_DROPLET, Localization::GetString("COMPOSITION_OPACITY").c_str());
            ImGui::PopItemWidth();
            if (!attributeOpacityUsed) t_composition->opacity = opacity;
            ImGui::EndChild();
            ImGui::InputTextWithHint("##blendFilter", FormatString("%s %s", ICON_FA_MAGNIFYING_GLASS, Localization::GetString("SEARCH_FILTER").c_str()).c_str(), &blendFilter);
            ImGui::SameLine(0, 0);
            searchBarWidth = ImGui::GetCursorPosX();
            ImGui::NewLine();
            ImGui::BeginChild("##blendCandidates", ImVec2(0, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);
                auto& blending = Compositor::s_blending;
                if (ImGui::MenuItem(FormatString("%s %s Normal", t_composition->blendMode.empty() ? ICON_FA_CHECK : "", ICON_FA_DROPLET).c_str())) {
                    t_composition->blendMode = "";
                }
                for (auto& mode : blending.modes) {
                    auto& project = Workspace::s_project.value();
                    if (!blendFilter.empty() && mode.name.find(blendFilter) == std::string::npos) continue;
                    if (ImGui::MenuItem(FormatString("%s %s %s", t_composition->blendMode == mode.codename ? ICON_FA_CHECK : "", Font::GetIcon(mode.icon).c_str(), mode.name.c_str()).c_str())) {
                        t_composition->blendMode = mode.codename;
                    }
                    if (ImGui::BeginItemTooltip()) {
                        auto& bundles = Compositor::s_bundles;
                        if (!IsInBounds(project.currentFrame, t_composition->beginFrame, t_composition->endFrame) || bundles.find(t_composition->id) == bundles.end()) {
                            ImGui::Text("%s %s", ICON_FA_TRIANGLE_EXCLAMATION, Localization::GetString("BLENDING_PREVIEW_IS_UNAVAILABLE").c_str());
                        } else {
                            auto& bundle = bundles[t_composition->id];
                            auto& primaryFramebuffer = Compositor::primaryFramebuffer.value();
                            auto requiredResolution = Compositor::GetRequiredResolution();
                            static Framebuffer previewFramebuffer = Compositor::GenerateCompatibleFramebuffer(requiredResolution);
                            if (previewFramebuffer.width != requiredResolution.x || previewFramebuffer.height != requiredResolution.y) {
                                for (auto& attachment : previewFramebuffer.attachments) {
                                    GPU::DestroyTexture(attachment);
                                }
                                GPU::DestroyFramebuffer(previewFramebuffer);
                                previewFramebuffer = Compositor::GenerateCompatibleFramebuffer(requiredResolution);
                            }
                            GPU::BindFramebuffer(previewFramebuffer);
                            GPU::ClearFramebuffer(project.backgroundColor.r, project.backgroundColor.g, project.backgroundColor.b, project.backgroundColor.a);
                            int compositionIndex = 0;
                            for (auto& candidate : project.compositions) {
                                if (candidate.id == t_composition->id) break;
                                compositionIndex++;
                            }
                            std::vector<int> allowedCompositions;
                            for (int i = 0; i < compositionIndex; i++) {
                                allowedCompositions.push_back(project.compositions[i].id);
                            }
                            GPU::BindFramebuffer(primaryFramebuffer);
                            GPU::ClearFramebuffer(project.backgroundColor.r, project.backgroundColor.g, project.backgroundColor.b, project.backgroundColor.a);
                            if (!allowedCompositions.empty()) Compositor::PerformComposition(allowedCompositions);
                            auto& blending = Compositor::s_blending;
                            blending.PerformBlending(mode, primaryFramebuffer.attachments[0], bundle.primaryFramebuffer.attachments[0], t_composition->GetOpacity());
                            GPU::BlitFramebuffer(previewFramebuffer, blending.framebufferCandidate.value().attachments[0]);

                            ImGui::Text("%s %s (%s)", Font::GetIcon(mode.icon).c_str(), mode.name.c_str(), mode.codename.c_str());
                            auto previewSize = FitRectInRect({128, 128}, {previewFramebuffer.width, previewFramebuffer.height});
                            ImGui::SetCursorPosX(ImGui::GetWindowSize().x / 2.0f - previewSize.x / 2.0f);
                            ImGui::Image(previewFramebuffer.attachments[0].handle, {previewSize.x, previewSize.y});

                        }
                        ImGui::EndTooltip();
                    }
                }
                fullWindowSize = ImGui::GetWindowSize();
            ImGui::EndChild();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(FormatString("%s %s", ICON_FA_PENCIL, Localization::GetString("EDIT_METADATA").c_str()).c_str())) {
            ImGui::SeparatorText(FormatString("%s %s", ICON_FA_PENCIL, Localization::GetString("EDIT_METADATA").c_str()).c_str());
            ImGui::InputTextWithHint("##compositionName", FormatString("%s %s", ICON_FA_PENCIL, Localization::GetString("COMPOSITION_NAME")).c_str(), &t_composition->name);
            ImGui::SetItemTooltip("%s %s", ICON_FA_PENCIL, Localization::GetString("COMPOSITION_NAME").c_str());
            ImGui::InputTextMultiline("##compositionDescription", &t_composition->description);
            ImGui::SetItemTooltip("%s %s", ICON_FA_PENCIL, Localization::GetString("COMPOSITION_DESCRIPTION").c_str());
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_ARROW_POINTER, Localization::GetString("SELECT_COMPOSITION").c_str()).c_str())) {
            AppendSelectedCompositions(t_composition);
        }
        if (ImGui::MenuItem(FormatString("%s %s", t_composition->enabled ? ICON_FA_TOGGLE_ON : ICON_FA_TOGGLE_OFF, Localization::GetString("ENABLE_DISABLE_COMPOSITIONS").c_str()).c_str())) {
            for (auto& compositionID : project.selectedCompositions) {
                auto compositionCandidate = Workspace::GetCompositionByID(compositionID);
                if (compositionCandidate.has_value()) {
                    auto& composition = compositionCandidate.value();
                    composition->enabled = !composition->enabled;
                }
            }
        }
        if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_COPY, Localization::GetString("COPY_SELECTED_COMPOSITIONS").c_str()).c_str(), "Ctrl+C")) {
            ProcessCopyAction();
        }
        if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_PASTE, Localization::GetString("PASTE_SELECTED_COMPOSITIONS").c_str()).c_str(), "Ctrl+V")) {
            ProcessPasteAction();
        }
        if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_CLONE, Localization::GetString("DUPLICATE_SELECTED_COMPOSITIONS").c_str()).c_str(), "Ctrl+D")) {
            ProcessCopyAction();
            ProcessPasteAction();
        }
        if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_TRASH_CAN, Localization::GetString("DELETE_SELECTED_COMPOSITIONS").c_str()).c_str(), "Delete")) {
            ProcessDeleteAction();
        }
    }

    void TimelineUI::ProcessCopyAction() {
        std::vector<Composition> copiedCompositions;
        auto& project = Workspace::GetProject();
        for (auto& selectedComposition : project.selectedCompositions) {
            auto compositionCandidate = Workspace::GetCompositionByID(selectedComposition);
            if (compositionCandidate.has_value()) {
                auto& composition = compositionCandidate.value();
                Composition copiedComposition = Composition(composition->Serialize());
                copiedComposition.name += " (" + Localization::GetString("COPY") + ")";
                copiedComposition.id = Randomizer::GetRandomInteger();
                for (auto& attribute : copiedComposition.attributes) {
                    attribute = Attributes::CopyAttribute(attribute).value();
                }
                std::unordered_map<int, int> idReplacements;
                for (auto& node : copiedComposition.nodes) {
                    int originalID = node->nodeID;
                    node->nodeID = Randomizer::GetRandomInteger();
                    idReplacements[originalID] = node->nodeID;

                    if (node->flowInputPin.has_value()) {
                        UpdateCopyPin(node->flowInputPin.value(), idReplacements);
                    }
                    if (node->flowOutputPin.has_value()) {
                        UpdateCopyPin(node->flowOutputPin.value(), idReplacements);
                    }
                    for (auto& inputPin : node->inputPins) {
                        UpdateCopyPin(inputPin, idReplacements);
                    }
                    for (auto& outputPin : node->outputPins) {
                        UpdateCopyPin(outputPin, idReplacements);
                    }
                }
                
                for (auto& node : copiedComposition.nodes) {
                    if (node->flowInputPin.has_value()) {
                        ReplaceCopyPin(node->flowInputPin.value(), idReplacements);
                    }
                    if (node->flowOutputPin.has_value()) {
                        ReplaceCopyPin(node->flowOutputPin.value(), idReplacements);
                    }
                    for (auto& inputPin : node->inputPins) {
                        ReplaceCopyPin(inputPin, idReplacements);
                    }
                    for (auto& outputPin : node->outputPins) {
                        ReplaceCopyPin(outputPin, idReplacements);
                    }
                }
                copiedCompositions.push_back(copiedComposition);
            }
        }
        s_copyCompositions = copiedCompositions;
    }

    void TimelineUI::ProcessPasteAction() {
        auto& project = Workspace::s_project.value();
        for (auto& composition : s_copyCompositions) {
            project.compositions.push_back(composition);
        }
    }

    void TimelineUI::ProcessDeleteAction() {
        auto& project = Workspace::GetProject();
        auto selectedCompositionsCopy = project.selectedCompositions;
        for (auto& compositionID : selectedCompositionsCopy) {
            auto compositionCandidate = Workspace::GetCompositionByID(compositionID);
            if (compositionCandidate.has_value()) {
                DeleteComposition(compositionCandidate.value());
            }
        }
    }

    void TimelineUI::UpdateCopyPin(GenericPin& pin, std::unordered_map<int, int>& idReplacements) {
        int originalID = pin.pinID;
        pin.pinID = Randomizer::GetRandomInteger();
        pin.linkID = Randomizer::GetRandomInteger();
        idReplacements[originalID] = pin.pinID;
    }

    void TimelineUI::ReplaceCopyPin(GenericPin& pin, std::unordered_map<int, int>& idReplacements) {
        if (idReplacements.find(pin.connectedPinID) != idReplacements.end()) {
            pin.connectedPinID = idReplacements[pin.connectedPinID];
        }
    }

    void TimelineUI::RenderNewAttributePopup(Composition* t_composition, ImGuiID t_parentTreeID) {
        ImGui::SeparatorText(FormatString("%s %s", ICON_FA_PLUS, Localization::GetString("ADD_ATTRIBUTE").c_str()).c_str());
        auto& project = Workspace::GetProject();
        for (auto& entry : Attributes::s_implementations) {
            if (ImGui::MenuItem(FormatString("%s %s %s", ICON_FA_PLUS, entry.description.prettyName.c_str(), Localization::GetString("ATTRIBUTE").c_str()).c_str())) {
                auto attributeCandidate = Attributes::InstantiateAttribute(entry.description.packageName);
                if (attributeCandidate.has_value()) {
                    t_composition->attributes.push_back(attributeCandidate.value());
                    if (!t_parentTreeID && s_compositionTrees.find(t_composition->id) != s_compositionTrees.end()) {
                        t_parentTreeID = s_compositionTrees[t_composition->id];
                    }
                    if (t_parentTreeID) {
                        s_legendTargetOpenTree = t_parentTreeID;
                        project.selectedAttributes = {attributeCandidate.value()->id};
                        if (s_compositionTreeScrolls.find(t_composition->id) != s_compositionTreeScrolls.end()) {
                            s_targetLegendScroll = s_compositionTreeScrolls[t_composition->id];
                        }
                    }
                }
            }
        }
    }

    void TimelineUI::RenderTimelinePopup() {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !s_layerPopupActive && ImGui::IsWindowFocused() && !UIShared::s_timelineBlockPopup) {
            ImGui::OpenPopup("##layerPopup");
        }
        UIShared::s_timelineBlockPopup = false;
        PopStyleVars();
        if (ImGui::BeginPopup("##layerPopup")) {
            ImGui::SeparatorText(FormatString("%s %s", ICON_FA_TIMELINE, Localization::GetString("TIMELINE").c_str()).c_str());
            if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_PLUS, Localization::GetString("NEW_COMPOSITION").c_str()).c_str())) {
                Workspace::s_project.value().compositions.push_back(Composition());
            }
            ImGui::EndPopup();
        }
        PushStyleVars();
    }
 
    void TimelineUI::RenderTimelineRuler() {
        ImGui::SetCursorPos({0, 0});
        auto& project = Workspace::s_project.value();
        s_timelineRulerDragged = s_timelineDrag.isActive;

        RectBounds timelineBounds(
            ImVec2(project.currentFrame * s_pixelsPerFrame, ImGui::GetScrollY()),
            ImVec2(TIMELINE_RULER_WIDTH, ImGui::GetWindowSize().y)
        );
        SetDrawListChannel(TimelineChannels::TimelineRuler);
        DrawRect(timelineBounds, ImVec4(1, 0, 0, 1));
        SetDrawListChannel(TimelineChannels::Compositions);

        if (MouseHoveringBounds(timelineBounds)) {
            s_timelineDrag.Activate();
        }

        float timelineDragDistance;
        if (s_timelineDrag.GetDragDistance(timelineDragDistance) && ImGui::IsWindowFocused() && !s_anyLayerDragged && !UIShared::s_timelineAnykeyframeDragged) {
            project.currentFrame = GetRelativeMousePos().x / s_pixelsPerFrame;
        } else s_timelineDrag.Deactivate();
    }



    void TimelineUI::RenderLegend() {
        Composition* targetCompositionDelete = nullptr;
        if (ImGui::BeginChild("##timelineLegend", ImVec2(ImGui::GetWindowSize().x * s_splitterState, ImGui::GetContentRegionAvail().y))) {
            SplitDrawList();
            if (s_targetLegendScroll > 0) {
                s_timelineScrollY = s_targetLegendScroll;
            }
            ImGui::SetScrollY(s_timelineScrollY);
            RenderTicksBar();
            RectBounds backgroundBounds = RectBounds(
                ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY()),
                ImVec2(ImGui::GetWindowSize().x, TICKS_BAR_HEIGHT)
            );
            auto& project = Workspace::s_project.value();
            static ImVec2 infoCentererSize = ImVec2(200, 20);
            ImGui::SetCursorPos(backgroundBounds.size / 2.0f - infoCentererSize / 2.0f);
            if (ImGui::BeginChild("##infoCenterer", ImVec2(0, 0), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY)) {
                ImGui::PushFont(Font::s_denseFont);
                ImGui::SetWindowFontScale(1.6f);
                static TimestampFormat timestampFormat = TimestampFormat::Regular;
                if (!project.customData.contains("TimelineTimestampFormat")) {
                    project.customData["TimelineTimestampFormat"] = static_cast<int>(timestampFormat);
                } else {
                    timestampFormat = static_cast<TimestampFormat>(project.customData["TimelineTimestampFormat"]);
                }
                std::string formattedTimestamp = project.FormatFrameToTime(project.currentFrame);
                if (timestampFormat == TimestampFormat::Frame) formattedTimestamp = std::to_string((int) project.currentFrame);
                if (timestampFormat == TimestampFormat::Seconds) formattedTimestamp = FormatString("%.2f", Precision(project.currentFrame / project.framerate, 2));
                ImVec2 timestampSize = ImGui::CalcTextSize(formattedTimestamp.c_str());
                ImGui::SetCursorPos(ImVec2(
                    5,
                    backgroundBounds.size.y / 2.0f - timestampSize.y / 2.0f
                ));
                static bool timestampHovered = false;
                ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                if (timestampHovered) textColor = textColor * 0.8f;
                ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                ImGui::Text("%s", formattedTimestamp.c_str());
                ImGui::PopStyleColor();
                timestampHovered = ImGui::IsItemHovered();
                if (timestampHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    ImGui::OpenPopup("##timestampFormatPopup");
                }
                PopStyleVars();
                ImGui::PopFont();
                ImGui::SetWindowFontScale(1.0f);
                if (ImGui::BeginPopup("##timestampFormatPopup")) {
                    ImGui::SeparatorText(FormatString("%s %s", ICON_FA_STOPWATCH, Localization::GetString("TIMESTAMP_FORMAT").c_str()).c_str());
                    if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_STOPWATCH, Localization::GetString("REGULAR").c_str()).c_str())) {
                        timestampFormat = TimestampFormat::Regular;
                    }
                    if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_STOPWATCH, Localization::GetString("FRAMES").c_str()).c_str())) {
                        timestampFormat = TimestampFormat::Frame;
                    }
                    if (ImGui::MenuItem(FormatString("%s %s", ICON_FA_STOPWATCH, Localization::GetString("SECONDS").c_str()).c_str())) {
                        timestampFormat = TimestampFormat::Seconds;
                    }
                    ImGui::EndPopup();
                }
                PushStyleVars();

                static ImVec2 searchFilterChildSize = ImVec2(200, 20);
                ImGui::SameLine(0, 6);
                ImGui::SetCursorPosY(
                    backgroundBounds.size.y / 2.0f - searchFilterChildSize.y / 2.0f
                );
                if (ImGui::BeginChild("##compositionSearchChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY)) {
                    static std::string s_newCompositionName = "";
                    if (ImGui::Button(ICON_FA_PLUS)) {
                        s_newCompositionName = "New Composition";
                        ImGui::OpenPopup("##createNewCompositionPopup");
                    }
                    ImGui::SameLine();

                    static bool createNewCompositionPopupFieldFocused = false;
                    PopStyleVars();
                    if (ImGui::BeginPopup("##createNewCompositionPopup")) {
                        if (!createNewCompositionPopupFieldFocused) ImGui::SetKeyboardFocusHere(0);
                        ImGui::InputTextWithHint("##newCompositionName", FormatString("%s %s", ICON_FA_LAYER_GROUP, Localization::GetString("NEW_COMPOSITION_NAME").c_str()).c_str(), &s_newCompositionName);
                        ImGui::SameLine();
                        if (ImGui::Button(FormatString("%s %s", ICON_FA_CHECK, Localization::GetString("OK").c_str()).c_str()) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                            Composition newComposition;
                            newComposition.name = s_newCompositionName;
                            if (s_colorMarkFilter != IM_COL32(0, 0, 0, 0)) newComposition.colorMark = s_colorMarkFilter;
                            project.compositions.push_back(newComposition);
                            ImGui::CloseCurrentPopup();
                        }
                        createNewCompositionPopupFieldFocused = true;

                        ImGui::EndPopup();
                    } else createNewCompositionPopupFieldFocused = false;
                    PushStyleVars();

                    PopStyleVars();
                    if (ImGui::ColorButton(FormatString("%s %s", ICON_FA_TAG, Localization::GetString("FILTER_BY_COLOR_MARK").c_str()).c_str(), ImGui::ColorConvertU32ToFloat4(s_colorMarkFilter), ImGuiColorEditFlags_AlphaPreview)) {
                        ImGui::OpenPopup("##filterByColorMark");
                    }

                    if (ImGui::BeginPopup("##filterByColorMark")) {
                        ImGui::SeparatorText(FormatString("%s %s", ICON_FA_FILTER, Localization::GetString("FILTER_BY_COLOR_MARK").c_str()).c_str());
                        static std::string s_colorMarkNameFilter = "";
                        ImGui::InputTextWithHint("##colorMarkFilter", FormatString("%s %s", ICON_FA_MAGNIFYING_GLASS, Localization::GetString("SEARCH_FILTER").c_str()).c_str(), &s_colorMarkNameFilter);
                        if (ImGui::BeginChild("##filterColorMarkCandidates", ImVec2(ImGui::GetContentRegionAvail().x, 220))) {
                            if (TextColorButton(Localization::GetString("NO_FILTER").c_str(), IM_COL32(0, 0, 0, 0))) {
                                s_colorMarkFilter = IM_COL32(0, 0, 0, 0);
                                ImGui::CloseCurrentPopup();
                            }
                            for (auto& pair : Workspace::s_colorMarks) {    
                                if (!s_colorMarkNameFilter.empty() && LowerCase(pair.first).find(LowerCase(s_colorMarkNameFilter)) == std::string::npos) continue;
                                if (TextColorButton(pair.first.c_str(), ImGui::ColorConvertU32ToFloat4(pair.second))) {
                                    s_colorMarkFilter = pair.second;
                                    ImGui::CloseCurrentPopup();
                                }
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndPopup();
                    }
                    PushStyleVars();

                    ImGui::SameLine();

                    ImGui::PushItemWidth(220);
                        ImGui::InputTextWithHint("##compositionSearch", FormatString("%s %s", ICON_FA_MAGNIFYING_GLASS, Localization::GetString("SEARCH_FILTER").c_str()).c_str(), &s_compositionFilter);
                    ImGui::PopItemWidth();
                    searchFilterChildSize = ImGui::GetWindowSize();
                }
                ImGui::EndChild();

                project.customData["TimelineTimestampFormat"] = static_cast<int>(timestampFormat);
                static bool timestampDragged = false;
                if ((timestampHovered || timestampDragged) && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowFocused()) {
                    project.currentFrame += ImGui::GetIO().MouseDelta.x / s_pixelsPerFrame;
                    project.currentFrame = std::clamp(project.currentFrame, 0.0f, project.GetProjectLength());
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    timestampDragged = true;
                } else timestampDragged = false;
                ImGui::SetWindowFontScale(1.0f);
                infoCentererSize = ImGui::GetWindowSize();
            }
            ImGui::EndChild();

            ImGui::SetCursorPos({0, backgroundBounds.size.y});

            float layerAccumulator = 0;
            for (int i = project.compositions.size(); i --> 0;) {
                auto& composition = project.compositions[i];
                if (!s_compositionFilter.empty() && LowerCase(composition.name).find(LowerCase(s_compositionFilter)) == std::string::npos) continue;
                if (s_colorMarkFilter != IM_COL32(0, 0, 0, 0) && composition.colorMark != s_colorMarkFilter) continue;
                std::string compositionName = FormatString("%s %s", ICON_FA_LAYER_GROUP, composition.name.c_str());
                ImVec2 compositionNameSize = ImGui::CalcTextSize(compositionName.c_str());
                ImVec2 baseCursor = ImVec2{
                    0, backgroundBounds.size.y + layerAccumulator
                };
                PopStyleVars();
                ImGui::SetCursorPos({5, backgroundBounds.size.y + layerAccumulator + LAYER_HEIGHT * 0.5f - compositionNameSize.y / 2.0f});
                ImGui::PushID(composition.id);
                if (ImGui::Button(ICON_FA_TRASH_CAN)) {
                    targetCompositionDelete = &composition;
                }
                ImGui::SameLine();
                if (ImGui::Button(composition.enabled ? ICON_FA_TOGGLE_ON : ICON_FA_TOGGLE_OFF)) {
                    composition.enabled = !composition.enabled;
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_PLUS)) {
                    ImGui::OpenPopup(FormatString("##createAttribute%i", composition.id).c_str());
                }
                if (ImGui::BeginPopup(FormatString("##createAttribute%i", composition.id).c_str())) {
                    RenderNewAttributePopup(&composition);
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                std::string colorMarkEditorPopupID = FormatString("##colorMarkEditorPopup%i", composition.id);
                bool colorMarkEditorPressed = ImGui::ColorButton(FormatString("%s %s", ICON_FA_TAG, Localization::GetString("COLOR_MARK").c_str()).c_str(), ImGui::ColorConvertU32ToFloat4(composition.colorMark), ImGuiColorEditFlags_AlphaPreview);
                if (colorMarkEditorPressed) {
                    ImGui::OpenPopup(colorMarkEditorPopupID.c_str());
                }
                if (ImGui::BeginPopup(colorMarkEditorPopupID.c_str())) {
                    ImGui::SeparatorText(FormatString("%s %s", ICON_FA_TAG, Localization::GetString("COLOR_MARK").c_str()).c_str());
                    static std::string s_colorMarkFilter = "";
                    ImGui::InputTextWithHint("##colorMarkFilter", FormatString("%s %s", ICON_FA_MAGNIFYING_GLASS, Localization::GetString("SEARCH_FILTER").c_str()).c_str(), &s_colorMarkFilter);
                    if (ImGui::BeginChild("##colorMarkCandidates", ImVec2(ImGui::GetContentRegionAvail().x, 210))) {
                        for (auto& colorPair : Workspace::s_colorMarks) {
                            ImVec4 v4ColorMark = ImGui::ColorConvertU32ToFloat4(colorPair.second);
                            if (TextColorButton(colorPair.first.c_str(), v4ColorMark)) {
                                composition.colorMark = colorPair.second;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                auto selectedIterator = std::find(project.selectedCompositions.begin(), project.selectedCompositions.end(), composition.id);
                s_compositionTreeScrolls[composition.id] = ImGui::GetScrollY();
                bool compositionTreeExpanded = ImGui::TreeNode(FormatString("%s%s###%i%s", selectedIterator != project.selectedCompositions.end() ? ICON_FA_ARROW_POINTER " " : "", compositionName.c_str(), composition.id, composition.name.c_str()).c_str());
                RenderLayerDragDrop(&composition);
                auto treeNodeID = ImGui::GetItemID();
                bool wasExpanded = false;
                s_compositionTrees[composition.id] = treeNodeID;
                s_attributesExpanded[composition.id] = compositionTreeExpanded;
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    ImGui::OpenPopup(FormatString("##accessibilityPopup%i", composition.id).c_str());
                }
                if (ImGui::BeginPopup(FormatString("##accessibilityPopup%i", composition.id).c_str())) {
                    RenderCompositionPopup(&composition, treeNodeID);
                    ImGui::EndPopup();
                }
                ImGui::SetCursorPos(baseCursor);
                ImGui::SetNextItemAllowOverlap();
                if (ImGui::InvisibleButton("##accessibilityButton", ImVec2(ImGui::GetWindowSize().x, LAYER_HEIGHT)) && !wasExpanded) {
                    ImGui::TreeNodeSetOpen(treeNodeID, !compositionTreeExpanded);
                }
                RenderLayerDragDrop(&composition);
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    ImGui::OpenPopup(FormatString("##compositionLegendPopup%i", composition.id).c_str());
                }

                ImVec2 reservedCursor = ImGui::GetCursorPos();
                ImGui::SetCursorPos({0, backgroundBounds.size.y + layerAccumulator + LAYER_HEIGHT - LAYER_SEPARATOR / 2.0f});
                RectBounds separatorBounds(
                    ImVec2(0, 0), 
                    ImVec2(ImGui::GetWindowSize().x, LAYER_SEPARATOR)
                );

                DrawRect(separatorBounds, ImVec4(0, 0, 0, 1));
                ImGui::SetCursorPos(reservedCursor);

                if (ImGui::BeginPopup(FormatString("##compositionLegendPopup%i", composition.id).c_str())) {
                    RenderCompositionPopup(&composition);
                    ImGui::EndPopup();
                }

                if (compositionTreeExpanded) {
                    float firstCursor = ImGui::GetCursorPosY();
                    if (composition.attributes.empty()) {
                        ImGui::Text("%s", Localization::GetString("NO_ATTRIBUTES").c_str());
                    }
                    for (auto& attribute : composition.attributes) {
                        s_attributeYCursors[attribute->id] = ImGui::GetCursorPosY();
                        float firstAttribueCursor = ImGui::GetCursorPosY();
                        attribute->RenderLegend(&composition);
                        UIShared::s_timelineAttributeHeights[composition.id] = ImGui::GetCursorPosY() - firstAttribueCursor;
                    }
                    ImGui::TreePop();
                    ImGui::Spacing();
                    s_legendOffsets[composition.id] = ImGui::GetCursorPosY() - firstCursor;
                }
                ImGui::PopID();
                PushStyleVars();

                layerAccumulator += LAYER_HEIGHT + s_legendOffsets[composition.id];
            }

            if (s_legendTargetOpenTree) {
                ImGui::TreeNodeSetOpen(s_legendTargetOpenTree, true);
                s_legendTargetOpenTree = 0;
            }
        }
        ProcessShortcuts();
        ImGui::EndChild();

        if (targetCompositionDelete) {
            DeleteComposition(targetCompositionDelete);
        }
    }

    void TimelineUI::RenderSplitter() {
        ImGui::SetCursorPos({0, 0});
        RectBounds splitterBounds(
            ImVec2(ImGui::GetWindowSize().x * s_splitterState - SPLITTER_RULLER_WIDTH / 2.0f, 0),
            ImVec2(SPLITTER_RULLER_WIDTH, ImGui::GetWindowSize().y)
        );

        RectBounds splitterLogicBounds(
            ImVec2(ImGui::GetWindowSize().x * s_splitterState - SPLITTER_RULLER_WIDTH, 0),
            ImVec2(SPLITTER_RULLER_WIDTH * 2, ImGui::GetWindowSize().y)
        );

        static bool splitterDragging = false;

        if (MouseHoveringBounds(splitterLogicBounds)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            DrawRect(splitterBounds, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                splitterDragging = true;   
            }
        }
        if (splitterDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && s_timelineFocused && !s_anyLayerDragged && !s_timelineRulerDragged && !UIShared::s_timelineAnykeyframeDragged) {
            s_splitterState = GetRelativeMousePos().x / ImGui::GetWindowSize().x;
        } else splitterDragging = false;

        s_splitterState = std::clamp(s_splitterState, 0.2f, 0.6f);
    }

    void TimelineUI::RenderLayerDragDrop(Composition* t_composition) {
        auto& project = Workspace::GetProject();
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(LAYER_REARRANGE_DRAG_DROP, &t_composition->id, sizeof(t_composition->id));
            TextColorButton(FormatString("%s %s", ICON_FA_LAYER_GROUP, t_composition->name.c_str()).c_str(), ImGui::ColorConvertU32ToFloat4(t_composition->colorMark));
            ImGui::Text("%s %s: %s (%0.1f)", ICON_FA_TIMELINE, Localization::GetString("IN_POINT").c_str(), project.FormatFrameToTime(t_composition->beginFrame).c_str(), t_composition->beginFrame);
            ImGui::Text("%s %s: %s (%0.1f)", ICON_FA_TIMELINE, Localization::GetString("OUT_POINT").c_str(), project.FormatFrameToTime(t_composition->endFrame).c_str(), t_composition->endFrame);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(LAYER_REARRANGE_DRAG_DROP)) {
                int fromID = *((int*) payload->Data);
                auto fromCompositionCandidate = Workspace::GetCompositionByID(fromID);
                if (fromCompositionCandidate.has_value()) {
                    auto& fromComposition = fromCompositionCandidate.value();
                    std::swap(*t_composition, *fromComposition);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    float TimelineUI::ProcessLayerScroll() {
        ImGui::SetCursorPos({0, 0});
        float mouseX = GetRelativeMousePos().x - ImGui::GetScrollX();
        DUMP_VAR(mouseX);
        float eventZone = ImGui::GetWindowSize().x / 10.0f;
        float mouseDeltaX = 5;
        if (mouseX > ImGui::GetWindowSize().x - eventZone && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImGui::SetScrollX(ImGui::GetScrollX() + mouseDeltaX);
            return mouseDeltaX;
        }

        if (mouseX < eventZone && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (ImGui::GetScrollX() + mouseDeltaX > 0) {
                ImGui::SetScrollX(ImGui::GetScrollX() + mouseDeltaX);
            }
            return -mouseDeltaX;
        }

        return 0;
    }

    ImVec2 TimelineUI::GetRelativeMousePos() {
        return ImGui::GetIO().MousePos - ImGui::GetCursorScreenPos();
    }
}