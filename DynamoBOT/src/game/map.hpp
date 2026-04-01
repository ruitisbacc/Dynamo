#pragma once

#include <string>
#include <vector>

namespace dynamo {

struct MapInfo {
    int32_t id{0};
    std::string name;
    int32_t width{0};
    int32_t height{0};
    int32_t fraction{0};        // War Universe map ownership
    bool pvp{true};

    struct Teleporter {
        int32_t id{0};
        float x{0}, y{0};
        int32_t targetMapId{0};
        float targetX{0}, targetY{0};
        bool active{true};
    };
    std::vector<Teleporter> teleporters;
};

} // namespace dynamo
