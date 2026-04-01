#pragma once

/**
 * @file combat_module.hpp
 * @brief Combat module - handles NPC targeting and killing
 *
 * Manages NPC target selection, approach, and attack sequences.
 */

#include "bot/core/module.hpp"
#include "bot/core/bot_config.hpp"
#include "bot/support/collection_helpers.hpp"
#include "bot/support/npc_database.hpp"
#include "bot/support/navigation.hpp"
#include "bot/support/movement_controller.hpp"
#include "game/game_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <regex>
#include <unordered_map>

namespace dynamo {

#include "bot/modules/combat/types.inl"

/**
 * @brief Combat module for NPC farming
 *
 * Responsibilities:
 * - Find best NPC target based on config priorities
 * - Approach target to attack range
 * - Select appropriate ammunition
 * - Attack and track until kill
 * - Anti-ban random movements
 */
class CombatModule : public Module {
public:
#include "bot/modules/combat/public.inl"

private:
#include "bot/modules/combat/members.inl"
#include "bot/modules/combat/core.inl"
#include "bot/modules/combat/movement.inl"
#include "bot/modules/combat/targeting.inl"
#include "bot/modules/combat/state_handlers.inl"
};

} // namespace dynamo
