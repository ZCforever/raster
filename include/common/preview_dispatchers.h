#pragma once
#include "raster.h"
#include "typedefs.h"

namespace Raster {
    struct PreviewDispatchers {
        static void DispatchStringValue(std::any& t_attribute);
        static void DispatchTextureValue(std::any& t_attribute);
        static void DispatchFloatValue(std::any& t_attribute);
        static void DispatchVector4Value(std::any& t_attribute);
        static void DispatchFramebufferValue(std::any& t_attribute);
        static void DispatchIntValue(std::any& t_attribute);
    };
};