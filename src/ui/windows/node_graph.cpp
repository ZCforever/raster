#include "node_graph.h"

namespace Raster {

    static ImVec2 s_headerSize, s_originalCursor;
    static float s_maxInputPinX, s_maxOutputPinX;

    static float s_pinTextScale = 0.7f;

    void NodeGraphUI::RenderInputPin(GenericPin& pin, bool flow) {
        ImVec2 linkedAttributeSize = ImGui::CalcTextSize(pin.linkedAttribute.c_str());
        Nodes::BeginPin(pin.pinID, Nodes::PinKind::Input);
            Nodes::PinPivotAlignment(ImVec2(-0.45f, 0.45f));

            if (!flow) Nodes::PinPivotAlignment(ImVec2(-0.115, 0.494997) * ImVec2(s_maxInputPinX / linkedAttributeSize.x, 1));
            if (!flow) ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 1);
            Widgets::Icon(ImVec2(20, linkedAttributeSize.y), flow ? Widgets::IconType::Flow : Widgets::IconType::Circle, pin.connectedPinID > 0);
            ImGui::SameLine(0, 2.0f);
            ImGui::SetWindowFontScale(s_pinTextScale);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.5f);
                ImGui::Text(pin.linkedAttribute.c_str());
            ImGui::SetWindowFontScale(1.0f);
        Nodes::EndPin();
    }

    void NodeGraphUI::RenderOutputPin(GenericPin& pin, bool flow) {
        ImVec2 linkedAttributeSize = ImGui::CalcTextSize(pin.linkedAttribute.c_str());
        ImGui::SetCursorPosX(s_originalCursor.x + (s_maxInputPinX + s_headerSize.x / 4.0f + (flow ? s_maxOutputPinX : 0)));
        Nodes::BeginPin(pin.pinID, Nodes::PinKind::Output);
            Nodes::PinPivotAlignment(ImVec2(1.44f, 0.45f));
            if (!flow) {
                Nodes::PinPivotAlignment(ImVec2(1.097, 0.521));
            }
            float reservedCursor = ImGui::GetCursorPosY();

            ImGui::SetCursorPosX(s_originalCursor.x + s_maxInputPinX + s_maxOutputPinX + s_headerSize.x / 4.0f);
            if (!flow) ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 1);
            bool beingUsed = false;
            for (auto& node : Workspace::s_nodes) {
                for (auto& inputPin : node->inputPins) {
                    if (inputPin.connectedPinID == pin.pinID) {
                        beingUsed = true;
                        break;
                    }
                }
                if (node->flowInputPin.has_value() && !beingUsed) {
                    beingUsed = node->flowInputPin.value().connectedPinID == pin.pinID;
                }
            }
            Widgets::Icon(ImVec2(20, linkedAttributeSize.y), flow ? Widgets::IconType::Flow : Widgets::IconType::Circle, beingUsed);
            ImGui::SetWindowFontScale(s_pinTextScale);
                ImGui::SetCursorPosY(reservedCursor + 2.5f);
                ImGui::SetCursorPosX((s_originalCursor.x + s_maxInputPinX + s_maxOutputPinX + s_headerSize.x / 4.0f - 5) - linkedAttributeSize.x * s_pinTextScale + 3);
                ImGui::Text(pin.linkedAttribute.c_str());
            ImGui::SetWindowFontScale(1.0f);
        Nodes::EndPin();
    }

    void NodeGraphUI::Render() {
        ImGui::Begin(FormatString("%s %s", ICON_FA_CIRCLE_NODES, Localization::GetString("NODE_GRAPH").c_str()).c_str());
            static Nodes::EditorContext* ctx = nullptr;
            if (!ctx) {
                Nodes::Config cfg;
                cfg.EnableSmoothZoom = true;
                ctx = Nodes::CreateEditor(&cfg);
            }

            Nodes::SetCurrentEditor(ctx);

            auto& style = Nodes::GetStyle();
            style.Colors[Nodes::StyleColor_Bg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
            style.Colors[Nodes::StyleColor_Grid] = ImVec4(0.09f, 0.09f, 0.09f, 1.0f);
            style.Colors[Nodes::StyleColor_NodeSelRect] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
            style.Colors[Nodes::StyleColor_NodeSelRectBorder] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
            style.NodeRounding = 0;
            style.NodeBorderWidth = 2.0f;
            style.SnapLinkToPinDir = 1;
            style.PinArrowSize = 10.0f;
            style.PinArrowWidth = 10.0f;


            Nodes::Begin("SimpleEditor");
                for (auto& node : Workspace::s_nodes) {
                    float maxInputXCandidate = 0;
                    for (auto& pin : node->inputPins) {
                        ImGui::SetWindowFontScale(s_pinTextScale);
                            auto attributeSize = ImGui::CalcTextSize(pin.linkedAttribute.c_str());
                        ImGui::SetWindowFontScale(1.0f);
                        if (attributeSize.x > maxInputXCandidate) {
                            maxInputXCandidate = attributeSize.x;
                        }
                    }
                    s_maxInputPinX = maxInputXCandidate;

                    float maxOutputXCandidate = 0;
                    for (auto& pin : node->outputPins) {
                        ImGui::SetWindowFontScale(s_pinTextScale);
                            auto attributeSize = ImGui::CalcTextSize(pin.linkedAttribute.c_str());
                        ImGui::SetWindowFontScale(1.0f);
                        if (attributeSize.x > maxOutputXCandidate) {
                            maxOutputXCandidate = attributeSize.x;
                        }
                    }
                    s_maxOutputPinX = maxOutputXCandidate;

                    Nodes::BeginNode(node->nodeID);
                        s_originalCursor = ImGui::GetCursorScreenPos();
                        ImVec2 originalCursor = ImGui::GetCursorScreenPos();
                        s_headerSize = ImGui::CalcTextSize(node->Header().c_str());
                        if (node->Footer().has_value()) {
                            auto footer = node->Footer().value();
                            ImGui::SetWindowFontScale(0.8f);
                            ImVec2 footerSize = ImGui::CalcTextSize(footer.c_str());
                            ImGui::SetWindowFontScale(1.0f);
                            if (footerSize.x > s_headerSize.x) {
                                s_headerSize = footerSize;
                            }
                        }
                        ImGui::Text("%s", node->Header().c_str());
                        if (node->flowInputPin.has_value()) {
                            RenderInputPin(node->flowInputPin.value(), true);
                        }
                        if (node->flowOutputPin.has_value()) {
                            ImGui::SameLine();
                            RenderOutputPin(node->flowOutputPin.value(), true);
                        }
                        float headerY = ImGui::GetCursorPosY();

                        ImGui::SetCursorPosX(originalCursor.x);

                        // Pins Rendering
                        for (auto& pin : node->inputPins) {
                            RenderInputPin(pin);
                        }


                        ImGui::SetCursorPosY(headerY);
                        for (auto& pin : node->outputPins) {
                            RenderOutputPin(pin);
                        }

                        // Footer Rendering

                        ImGui::SetWindowFontScale(0.8f);
                        auto footer = node->Footer();
                        if (footer.has_value()) {
                            ImGui::Spacing();
                            ImGui::Text(footer.value().c_str());
                        }
                        ImGui::SetWindowFontScale(1.0f);
                    Nodes::EndNode();
                }

                for (auto& node : Workspace::s_nodes) {
                    if (node->flowInputPin.has_value() && node->flowInputPin.value().connectedPinID > 0) {
                        auto sourcePin = node->flowInputPin.value();
                        Nodes::Link(sourcePin.linkID, sourcePin.pinID, sourcePin.connectedPinID, ImVec4(1, 1, 1, 1), 2.0f);
                    }
                    if (node->flowOutputPin.has_value() && node->flowOutputPin.value().connectedPinID > 0) {
                        auto sourcePin = node->flowOutputPin.value();
                        Nodes::Link(sourcePin.linkID, sourcePin.pinID, sourcePin.connectedPinID, ImVec4(1, 1, 1, 1), 2.0f);
                    }

                    for (auto& pin : node->inputPins) {
                        if (pin.connectedPinID > 0) {
                            Nodes::Link(pin.linkID, pin.pinID, pin.connectedPinID, ImVec4(1, 1, 1, 1), 2.0f);
                        }
                    }
                    for (auto& pin : node->outputPins) {
                        if (pin.connectedPinID > 0) {
                            Nodes::Link(pin.linkID, pin.pinID, pin.connectedPinID, ImVec4(1, 1, 1, 1), 2.0f);
                        }
                    }
                }

                if (Nodes::BeginCreate()) {
                    auto showLabel = [](const char* label, ImColor color) {
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
                        auto size = ImGui::CalcTextSize(label);

                        auto padding = ImGui::GetStyle().FramePadding;
                        auto spacing = ImGui::GetStyle().ItemSpacing;

                        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

                        auto rectMin = ImGui::GetCursorScreenPos() - padding;
                        auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

                        auto drawList = ImGui::GetWindowDrawList();
                        drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                        ImGui::TextUnformatted(label);
                    };
                    Nodes::PinId startPinID, endPinID;
                    if (Nodes::QueryNewLink(&startPinID, &endPinID)) {
                        int rawStartPinID = (int) startPinID.Get();
                        int rawEndPinID = (int) endPinID.Get();
                        auto startNode = Workspace::GetNodeByPinID(rawStartPinID);
                        auto endNode = Workspace::GetNodeByPinID(rawEndPinID);
                        
                        auto startPinContainer = Workspace::GetPinByPinID(rawStartPinID);
                        auto endPinContainer = Workspace::GetPinByPinID(rawEndPinID);
                        if (startNode.has_value() && endNode.has_value() && startPinContainer.has_value() && endPinContainer.has_value()) {
                            auto startPin = startPinContainer.value();
                            auto endPin = endPinContainer.value();

                            if (startPin.type == PinType::Input) {
                                std::swap(startPin, endPin);
                                std::swap(startPinContainer, endPinContainer);
                                std::swap(startPinID, endPinID);
                                std::swap(rawStartPinID, rawEndPinID);
                            }

                            if (endPin.pinID == startPin.pinID) {
                                Nodes::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                            } else if (endPin.type == startPin.type) {
                                showLabel(FormatString("%s %s", ICON_FA_XMARK, Localization::GetString("INVALID_LINK").c_str()).c_str(), ImColor(45, 32, 32, 180));
                                Nodes::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                            } else {
                                if (Nodes::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                                    // startPin.connectedPinID = endPin.pinID;
                                    endPin.connectedPinID = startPin.pinID;
                                }
                            }

                            Workspace::UpdatePinByID(startPin, startPin.pinID);
                            Workspace::UpdatePinByID(endPin, endPin.pinID);
                        }
                    }
                }
                Nodes::EndCreate();

                if (Nodes::BeginDelete()) {
                    Nodes::NodeId nodeID = 0;
                    while (Nodes::QueryDeletedNode(&nodeID)) {
                        int rawNodeID = (int) nodeID.Get();
                        if (Nodes::AcceptDeletedItem()) {
                            int targetNodeDelete = -1;
                            int nodeIndex = 0;
                            for (auto& node : Workspace::s_nodes) {
                                if (node->nodeID == rawNodeID) {
                                    targetNodeDelete = nodeIndex;
                                    break; 
                                }
                                nodeIndex++;
                            }
                            Workspace::s_nodes.erase(Workspace::s_nodes.begin() + targetNodeDelete);
                        }
                    }

                    Nodes::LinkId linkID = 0;
                    while (Nodes::QueryDeletedLink(&linkID)) {
                        int rawLinkID = (int) linkID.Get();
                        auto pin = Workspace::GetPinByLinkID(rawLinkID);
                        if (pin.has_value()) {
                            auto correctedPin = pin.value();
                            correctedPin.connectedPinID = -1;
                            Workspace::UpdatePinByID(correctedPin, correctedPin.pinID);
                        }
                    }
                }
                Nodes::EndDelete();

            Nodes::Suspend();
            if (Nodes::ShowBackgroundContextMenu()) {
                ImGui::OpenPopup("##createNewNode");
            }
            Nodes::Resume();

            Nodes::Suspend();
            static float dimA = 0.0f;
            static float dimB = 0.3f;
            static float dimPercentage = -1.0f;
            static float previousDimPercentage;
            static bool dimming = false;
            previousDimPercentage = dimPercentage;
            if (dimPercentage >= 0 && dimming) {
                dimPercentage += ImGui::GetIO().DeltaTime * 2.5;
                if (dimPercentage >= dimB) {
                    dimming = false;
                }
                dimPercentage = std::clamp(dimPercentage, 0.0f, dimB);
            }
            bool popupVisible = true;
            if (ImGui::BeginPopup("##createNewNode")) {
                static std::string searchFilter = "";
                ImGui::InputText("##searchFilter", &searchFilter);
                ImGui::SetItemTooltip(FormatString("%s %s", ICON_FA_MAGNIFYING_GLASS, Localization::GetString("SEARCH_FILTER").c_str()).c_str());
                ImGui::EndPopup();
                if (dimPercentage < 0) {
                    dimPercentage = 0.0f;
                    dimming = true;
                }
            } else {
                dimming = false;
                popupVisible = false;
            }
            if (!popupVisible) {
                dimPercentage -= ImGui::GetIO().DeltaTime * 2.5;
                if (dimPercentage < 0) {
                    dimPercentage = -1;
                    dimming = true;
                }
            }
            Nodes::Resume();
            Nodes::End();
            Nodes::SetCurrentEditor(nullptr);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
    
            if (dimPercentage > 0) {
                drawList->AddRectFilled(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(), ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, dimPercentage)));
            }
        ImGui::End();
    }
}