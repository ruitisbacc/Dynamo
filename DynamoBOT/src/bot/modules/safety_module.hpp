#pragma once

/**
 * @file safety_module.hpp
 * @brief Safety module - handles emergency flee, repair, enemy detection
 *
 * Highest priority module. Takes over when danger is detected.
 */

#include "bot/core/module.hpp"
#include "bot/core/bot_config.hpp"
#include "bot/support/threat_tracker.hpp"
#include "bot/support/navigation.hpp"
#include "bot/support/movement_controller.hpp"
#include "game/game_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

namespace dynamo {

#include "bot/modules/safety/types.inl"

/**
 * @brief Safety and emergency flee module
 *
 * Responsibilities:
 * - Monitor for enemy players
 * - Flee when attacked or HP too low
 * - Find and use escape portals
 * - Switch to escape ship configuration
 * - Handle repair/recovery
 */
class SafetyModule : public Module {
public:
#include "bot/modules/safety/public.inl"

private:
#include "bot/modules/safety/members.inl"
#include "bot/modules/safety/core.inl"
#include "bot/modules/safety/anchors.inl"
#include "bot/modules/safety/state_handlers.inl"
};

} // namespace dynamo
