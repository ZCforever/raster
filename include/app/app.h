#pragma once

#include "raster.h"
#include "ui/ui.h"

namespace Raster {
    struct App {
        static std::vector<AbstractUI> s_windows;

        static void Initialize();
        static void RenderLoop();
        static void Terminate();
    };
};