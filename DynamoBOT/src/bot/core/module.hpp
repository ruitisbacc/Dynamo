#pragma once

/**
 * @file module.hpp
 * @brief Base class for bot behavior modules
 * 
 * War Universe Bot - DynamoBot
 * Inspired by DarkOrbit bot architecture, adapted for WU.
 */

#include "bot/core/bot_config.hpp"
#include "game/hero.hpp"
#include "game/entities.hpp"

#include <string>
#include <memory>
#include <string_view>

namespace dynamo {

// Forward declarations
class GameEngine;
class MovementController;

/**
 * @brief Snapshot of current game state passed to modules
 * 
 * Combines hero and entities snapshots for convenience.
 */
struct GameSnapshot {
    HeroSnapshot hero;
    EntitiesSnapshot entities;
    
    int64_t timestampMs{0};
    int32_t mapId{0};
    std::string mapName;
    bool inSafeZone{false};
    BotMode mode{BotMode::KillCollect};
    
    // Convenience: Find ship by ID
    std::optional<ShipInfo> findShip(int32_t id) const {
        return entities.findShip(id);
    }
    
    // Convenience: Find box by ID
    std::optional<BoxInfo> findBox(int32_t id) const {
        return entities.findBox(id);
    }
};

/**
 * @brief Abstract base class for all bot modules
 * 
 * Modules are behavior units that compete for execution time.
 * The scheduler selects the module with highest priority each tick.
 * 
 * Lifecycle:
 *   1. getPriority() - Called every tick to determine if module wants to run
 *   2. onStart() - Called when module becomes active
 *   3. tick() - Called every frame while module is active
 *   4. onStop() - Called when another module takes over
 */
class Module {
public:
    Module(std::shared_ptr<GameEngine> engine, std::shared_ptr<MovementController> movement)
        : engine_(std::move(engine))
        , movement_(std::move(movement)) {}
    
    virtual ~Module() = default;
    
    // Non-copyable, movable
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = default;
    Module& operator=(Module&&) = default;
    
    /**
     * @brief Unique name of this module for logging/debugging
     */
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    
    /**
     * @brief Returns priority of this module given current game state
     * 
     * Higher value = higher priority.
     * Return <= 0 if module doesn't want to run.
     * 
     * Typical priority ranges:
     *   - Safety/Emergency: 80-100
     *   - Combat: 50-70
     *   - Collection: 30-50
     *   - Idle/Roaming: 1-20
     * 
     * @param snap Current game state snapshot
     * @return Priority value, or <= 0 to not run
     */
    [[nodiscard]] virtual int getPriority(const GameSnapshot& snap) = 0;
    
    /**
     * @brief Execute module logic for one tick
     * 
     * Called every frame while this module is active.
     * 
     * @param snap Current game state snapshot
     */
    virtual void tick(const GameSnapshot& snap) = 0;
    
    /**
     * @brief Called when module becomes the active one
     * 
     * Use for initialization, logging, etc.
     */
    virtual void onStart() {}
    
    /**
     * @brief Called when module stops being active
     * 
     * Use for cleanup, state reset, etc.
     */
    virtual void onStop() {}
    
    /**
     * @brief Check if module is currently enabled
     */
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    
    /**
     * @brief Enable or disable this module
     */
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }

protected:
    std::shared_ptr<GameEngine> engine_;
    std::shared_ptr<MovementController> movement_;
    bool enabled_{true};
};

} // namespace dynamo
