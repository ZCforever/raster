#include "sdf_rounded_rect.h"
#include "node_category/node_category.h"
#include "common/workspace.h"

namespace Raster {

    SDFRoundedRect::SDFRoundedRect() {
        NodeBase::Initialize();

        SetupAttribute("Rounding", 0.5f);

        AddOutputPin("Shape");
    }

    AbstractPinMap SDFRoundedRect::AbstractExecute(AbstractPinMap t_accumulator) {
        AbstractPinMap result = {};

        auto roundingCandidate = GetAttribute<float>("Rounding");
        if (roundingCandidate.has_value()) {
            auto& radius = roundingCandidate.value();
            static SDFShape s_roundedRectShape;
            if (s_roundedRectShape.uniforms.empty()) {
                s_roundedRectShape.distanceFunctionName = "fSDFRoundedRect";
                s_roundedRectShape.distanceFunctionCode = ReadFile(GPU::GetShadersPath() + "sdf_shapes/sdf_rounded_rect.frag");
            }
            s_roundedRectShape.uniforms = {
                SDFShapeUniform("float", "uSDFRoundedRectRadius", radius)
            };
            TryAppendAbstractPinMap(result, "Shape", s_roundedRectShape);
        }

        return result;
    }

    void SDFRoundedRect::AbstractRenderProperties() {
        RenderAttributeProperty("Rounding");
    }

    void SDFRoundedRect::AbstractLoadSerialized(Json t_data) {
        SetAttributeValue("Rounding", t_data["Rounding"].get<float>());    
    }

    Json SDFRoundedRect::AbstractSerialize() {
        return {
            {"Rounding", RASTER_ATTRIBUTE_CAST(float, "Rounding")}
        };
    }

    bool SDFRoundedRect::AbstractDetailsAvailable() {
        return false;
    }

    std::string SDFRoundedRect::AbstractHeader() {
        return "SDF Rounded Rect";
    }

    std::string SDFRoundedRect::Icon() {
        return ICON_FA_SQUARE;
    }

    std::optional<std::string> SDFRoundedRect::Footer() {
        return std::nullopt;
    }
}

extern "C" {
    Raster::AbstractNode SpawnNode() {
        return (Raster::AbstractNode) std::make_shared<Raster::SDFRoundedRect>();
    }

    void OnStartup() {
        Raster::NodeCategoryUtils::RegisterCategory(ICON_FA_SHAPES, "Shapes");

        Raster::Workspace::s_typeColors[ATTRIBUTE_TYPE(Raster::SDFShape)] = RASTER_COLOR32(52, 235, 222, 255);
        Raster::Workspace::s_typeNames[ATTRIBUTE_TYPE(Raster::SDFShape)] = "SDFShape";
    }

    Raster::NodeDescription GetDescription() {
        return Raster::NodeDescription{
            .prettyName = "SDF Rounded Rect",
            .packageName = RASTER_PACKAGED "sdf_rounded_rect",
            .category = Raster::NodeCategoryUtils::RegisterCategory(ICON_FA_SHAPES, "Shapes")
        };
    }
}