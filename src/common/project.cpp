#include "raster.h"
#include "common/common.h"

namespace Raster {
    Project::Project(Json data) {
        this->name = data["Name"];
        this->description = data["Description"];
        this->framerate = data["Framerate"];
        this->currentFrame = data["CurrentFrame"];
        this->preferredResolution = {
            (float) data["PreferredResolution"][0],
            (float) data["PreferredResolution"][1]
        };
        this->backgroundColor = {
            (float) data["BackgroundColor"][0],
            (float) data["BackgroundColor"][1],
            (float) data["BackgroundColor"][2],
            (float) data["BackgroundColor"][3]
        };
        for (auto& composition : data["Compositions"]) {
            compositions.push_back(Composition(composition));
        }
    }

    Project::Project() {
        this->name = "Empty Project";
        this->description = "Nothing...";
        this->framerate = 60;
        this->currentFrame = 0;
        this->preferredResolution = {
            1080, 1080
        };
        this->backgroundColor = {0, 0, 0, 1};
    }

    uint64_t Project::GetProjectLength() {
        uint64_t candidate = UINT64_MAX;
        for (auto& composition : compositions) {
            if (composition.endFrame < candidate) {
                candidate = composition.endFrame;
            }
        }
        return candidate == UINT64_MAX ? 0 : candidate;
    }

    std::string Project::FormatFrameToTime(uint64_t frame) {
        auto transformedFrame = frame / framerate;
        float minutes = std::floor(transformedFrame / 60);
        float seconds = std::floor(remainder(transformedFrame, 60.0f));
        return FormatString("%02i:%02i", (int) minutes, (int) seconds);
    }

    Json Project::Serialize() {
        Json data = {};

        data["Name"] = name;
        data["Description"] = description;
        data["Framerate"] = framerate;
        data["CurrentFrame"] = currentFrame;
        data["PreferredResolution"] = {
            preferredResolution.x, preferredResolution.y
        };
        data["BackgroundColor"] = {
            backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a
        };
        data["Compositions"] = {};
        for (auto& composition : compositions) {
            data["Compositions"].push_back(composition.Serialize());
        }

        return data;
    }
};