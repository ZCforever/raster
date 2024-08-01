#include "sampler_constants_base/sampler_constants_base.h"
#include "gpu/gpu.h"

extern "C" {
    Raster::AbstractNode SpawnNode() {
        return (Raster::AbstractNode) std::make_shared<Raster::SamplerConstantsBase>(Raster::SamplerConstantsBase(Raster::TextureWrappingMode::ClampToBorder));
    }

    void OnStartup() {
        Raster::NodeCategoryUtils::RegisterCategory(ICON_FA_IMAGE, Raster::Localization::GetString("SAMPLER_CONSTANTS"));
    }

    Raster::NodeDescription GetDescription() {
        return Raster::NodeDescription{
            .prettyName = "Clamp To Border Sampler Wrapping",
            .packageName = RASTER_PACKAGED "clamp_to_border_sampler_wrapping_constant",
            .category = Raster::NodeCategoryUtils::RegisterCategory(ICON_FA_IMAGE, Raster::Localization::GetString("SAMPLER_CONSTANTS"))
        };
    }
}