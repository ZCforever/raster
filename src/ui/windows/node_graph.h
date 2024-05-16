#pragma once

#include "raster.h"
#include "ui/ui.h"
#include "common/common.h"
#include "utils/widgets.h"

#include "../../ImGui/imgui.h"
#include "../../ImGui/imgui_node_editor.h"

namespace Raster {

    namespace Nodes = ax::NodeEditor;
    namespace Widgets = ax::Widgets;

    struct NodeGraphUI : public UI {
        void Render();

        void RenderInputPin(GenericPin& pin, bool flow = false);
        void RenderOutputPin(GenericPin& pin, bool flow = false);
    };
};