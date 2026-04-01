#pragma once

/**
 * @file game.hpp
 * @brief Supported backend surface for DynamoBot
 * 
 * This is the only supported backend entry point for runtime code.
 * It exposes the production connection/state/action pipeline centered
 * around GameEngine and the snapshot types consumed by GUI and bot code.
 */

// Core engine
#include "game/game_engine.hpp"

// Entity management
#include "game/map.hpp"
#include "game/entities.hpp"
#include "game/hero.hpp"
#include "game/resource_state.hpp"

// Action system
#include "game/action_queue.hpp"
#include "game/actions.hpp"
