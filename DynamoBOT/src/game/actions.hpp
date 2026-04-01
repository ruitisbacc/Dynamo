#pragma once

#include "game/game_engine.hpp"
#include <string>
#include <cmath>

namespace dynamo::actions {

/**
 * @brief Action helper functions for GameEngine
 * 
 * These provide a clean API similar to DarkOrbit's Actions.cpp,
 * with built-in throttling and convenience methods.
 */

//------------------------------------------------------------------------------
// Movement
//------------------------------------------------------------------------------

/**
 * @brief Move to absolute position
 */
inline void moveTo(GameEngine& engine, float x, float y) {
    if (!engine.canSend()) return;
    engine.moveTo(x, y);
}

/**
 * @brief Move relative to current position
 */
inline void moveRelative(GameEngine& engine, float dx, float dy) {
    if (!engine.canSend()) return;
    auto hero = engine.hero();
    engine.moveTo(hero.x + dx, hero.y + dy);
}

/**
 * @brief Move towards a target at given distance
 */
inline void moveTowards(GameEngine& engine, float targetX, float targetY, float maxDistance) {
    if (!engine.canSend()) return;
    auto hero = engine.hero();
    
    float dx = targetX - hero.x;
    float dy = targetY - hero.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    
    if (dist <= maxDistance) {
        engine.moveTo(targetX, targetY);
    } else {
        float ratio = maxDistance / dist;
        engine.moveTo(hero.x + dx * ratio, hero.y + dy * ratio);
    }
}

/**
 * @brief Move to random point within bounds
 */
inline void moveRandom(GameEngine& engine, float minX, float maxX, float minY, float maxY) {
    if (!engine.canSend()) return;
    
    float x = minX + static_cast<float>(std::rand()) / RAND_MAX * (maxX - minX);
    float y = minY + static_cast<float>(std::rand()) / RAND_MAX * (maxY - minY);
    engine.moveTo(x, y);
}

/**
 * @brief Move to random point on current map
 */
inline void moveRandomOnMap(GameEngine& engine) {
    if (!engine.canSend()) return;
    
    auto map = engine.mapInfo();
    if (map.width <= 0 || map.height <= 0) return;
    
    // Stay 500 units from edges
    float margin = 500.0f;
    moveRandom(engine, margin, map.width - margin, margin, map.height - margin);
}

//------------------------------------------------------------------------------
// Combat
//------------------------------------------------------------------------------

/**
 * @brief Select/lock a target
 */
inline void selectTarget(GameEngine& engine, int32_t targetId) {
    if (!engine.canSend()) return;
    engine.lockTarget(targetId);
}

/**
 * @brief Deselect current target
 */
inline void deselectTarget(GameEngine& engine) {
    if (!engine.canSend()) return;
    engine.lockTarget(0);
}

/**
 * @brief Start attacking current target
 */
inline void attackLaser(GameEngine& engine) {
    if (!engine.canSend()) return;
    engine.attack();
}

/**
 * @brief Stop attacking
 */
inline void stopAttack(GameEngine& engine) {
    if (!engine.canSend()) return;
    engine.stopAttack();
}

/**
 * @brief Full attack sequence: select + attack
 */
inline void attackTarget(GameEngine& engine, int32_t targetId) {
    if (!engine.canSend()) return;
    engine.lockTarget(targetId);
    // Small delay then attack (handled by action queue)
    engine.attack();
}

/**
 * @brief Switch ammo type (1-6)
 */
inline void switchAmmo(GameEngine& engine, int32_t ammoType) {
    if (!engine.canSend()) return;
    engine.switchAmmo(ammoType);
}

/**
 * @brief Switch rocket type
 */
inline void switchRocket(GameEngine& engine, int32_t rocketType) {
    if (!engine.canSend()) return;
    engine.switchRocket(rocketType);
}

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

/**
 * @brief Switch to config 1 or 2
 */
inline void switchConfig(GameEngine& engine, int32_t configIndex) {
    if (!engine.canSend()) return;
    engine.switchConfig(configIndex);
}

/**
 * @brief Toggle between config 1 and 2
 */
inline void toggleConfig(GameEngine& engine) {
    if (!engine.canSend()) return;
    auto hero = engine.hero();
    int newConfig = (hero.activeConfig == 1) ? 2 : 1;
    engine.switchConfig(newConfig);
}

//------------------------------------------------------------------------------
// Collection
//------------------------------------------------------------------------------

/**
 * @brief Collect a box/resource
 */
inline void collectBox(GameEngine& engine, int32_t collectableId) {
    if (!engine.canSend()) return;
    engine.collect(collectableId);
}

/**
 * @brief Move to box and collect
 */
inline void moveAndCollect(GameEngine& engine, int32_t collectableId, float boxX, float boxY) {
    if (!engine.canSend()) return;
    
    auto hero = engine.hero();
    float dist = std::sqrt(std::pow(boxX - hero.x, 2) + std::pow(boxY - hero.y, 2));
    
    if (dist > 100) {
        // Move closer first (with offset for collection)
        engine.moveTo(boxX, boxY + 95);
    } else {
        engine.collect(collectableId);
    }
}

//------------------------------------------------------------------------------
// Abilities
//------------------------------------------------------------------------------

/**
 * @brief Use ability by type
 */
inline void useAbility(GameEngine& engine, int32_t abilityId) {
    if (!engine.canSend()) return;
    engine.useAbility(abilityId);
}

/**
 * @brief Teleport through nearest portal
 */
inline void teleport(GameEngine& engine) {
    if (!engine.canSend()) return;
    engine.teleport();
}

//------------------------------------------------------------------------------
// Lifecycle
//------------------------------------------------------------------------------

/**
 * @brief Request revive after death
 */
inline void revive(GameEngine& engine) {
    engine.revive();
}

/**
 * @brief Logout from game
 */
inline void logout(GameEngine& engine) {
    engine.logout();
}

//------------------------------------------------------------------------------
// Utility
//------------------------------------------------------------------------------

/**
 * @brief Calculate distance between two points
 */
inline float distance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

/**
 * @brief Calculate angle from point 1 to point 2 (radians)
 */
inline float angle(float x1, float y1, float x2, float y2) {
    return std::atan2(y2 - y1, x2 - x1);
}

/**
 * @brief Calculate point at distance/angle from origin
 */
inline std::pair<float, float> pointAt(float originX, float originY, float dist, float angleRad) {
    return {
        originX + std::cos(angleRad) * dist,
        originY + std::sin(angleRad) * dist
    };
}

/**
 * @brief Check if position is within map bounds
 */
inline bool isInBounds(const MapInfo& map, float x, float y, float margin = 0) {
    return x >= margin && x <= map.width - margin &&
           y >= margin && y <= map.height - margin;
}

/**
 * @brief Clamp position to map bounds
 */
inline std::pair<float, float> clampToBounds(const MapInfo& map, float x, float y, float margin = 100) {
    float clampedX = std::max(margin, std::min(x, static_cast<float>(map.width) - margin));
    float clampedY = std::max(margin, std::min(y, static_cast<float>(map.height) - margin));
    return {clampedX, clampedY};
}

} // namespace dynamo::actions
