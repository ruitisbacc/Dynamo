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
#include "safety_module.hpp"
#include "combat_module.hpp"
#include "collect_module.hpp"
#include "roaming_module.hpp"
#include "travel_module.hpp"

// Utilities
#include "movement_controller.hpp"
#include "threat_tracker.hpp"
#include "navigation.hpp"
#include "map_graph.hpp"
#include "npc_database.hpp"
