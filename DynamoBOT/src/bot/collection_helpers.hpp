#pragma once

#include "navigation.hpp"
#include "../game/entities.hpp"

#include <cstdint>

namespace dynamo {

inline constexpr double kCollectApproachYOffset = 95.0;
inline constexpr int64_t kGreenBootyCollectWaitMs = 6000;

[[nodiscard]] inline Position collectApproachPosition(const BoxInfo& box) {
    return Position(box.x, box.y + kCollectApproachYOffset);
}

[[nodiscard]] inline int64_t collectPostAttemptWaitMs(const BoxInfo& box) {
    return box.isGreenBox() ? kGreenBootyCollectWaitMs : 0;
}

} // namespace dynamo
