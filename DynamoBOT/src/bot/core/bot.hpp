#pragma once

/**
 * @file bot.hpp
 * @brief Main include file for DynamoBot
 * 
 * Includes all bot components. Use this for easy integration.
 */

// Core components
#include "module.hpp"
#include "scheduler.hpp"
#include "bot_config.hpp"
#include "bot_controller.hpp"

// Modules
#include "bot/modules/safety_module.hpp"
#include "bot/modules/combat_module.hpp"
#include "bot/modules/collect_module.hpp"
#include "bot/modules/roaming_module.hpp"
#include "bot/modules/travel_module.hpp"

// Utilities
#include "bot/support/movement_controller.hpp"
#include "bot/support/threat_tracker.hpp"
#include "bot/support/navigation.hpp"
#include "bot/support/map_graph.hpp"
#include "bot/support/npc_database.hpp"
