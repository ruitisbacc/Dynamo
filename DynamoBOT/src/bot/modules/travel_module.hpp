#pragma once

/**
 * @file travel_module.hpp
 * @brief Travel module - handles inter-map navigation
 *
 * Automatically travels between maps using portal pathfinding.
 * Based on wupacket-main navigation.js
 */

#include "bot/core/module.hpp"
#include "bot/core/bot_config.hpp"
#include "bot/support/map_graph.hpp"
#include "bot/support/navigation.hpp"
#include "bot/support/movement_controller.hpp"
#include "game/game_engine.hpp"

#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_set>

namespace dynamo {

/**
 * @brief Travel state machine states
 */
enum class TravelState {
    Idle,               // Not traveling
    Planning,           // Finding path
    MovingToPortal,     // Moving towards portal
    AtPortal,           // Arrived at portal position
    Jumping,            // Teleporting through portal
    WaitingForMap,      // Waiting for new map to load
    Arrived             // Reached destination
};

struct TravelTelemetry {
    TravelState state{TravelState::Idle};
    std::string sourceMap;
    std::string destinationMap;
    std::string nextMap;
    std::string decision{"Idle"};
    int32_t currentHop{0};
    int32_t hopsRemaining{0};
    float portalDistance{0.0f};
    bool usingLivePortal{false};
    int32_t replans{0};
    int32_t recoveries{0};
    int64_t lastProgressMs{0};
    int64_t lastStateChangeMs{0};
};

/**
 * @brief Travel module for inter-map navigation
 *
 * Responsibilities:
 * - Calculate shortest path between maps using BFS
 * - Move to portal positions
 * - Execute teleport/jump actions
 * - Wait for map transitions
 * - Resume travel on new map until destination reached
 */
class TravelModule : public Module {
public:
    TravelModule(std::shared_ptr<GameEngine> engine,
                 std::shared_ptr<MovementController> movement,
                 const MapConfig& config)
        : Module(std::move(engine), std::move(movement))
        , config_(config) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Travel";
    }

    [[nodiscard]] int getPriority(const GameSnapshot& snap) override {
        if (!enabled_ || (!config_.enabled && !externalControl_)) {
            return 0;
        }

        if (targetMap_.empty()) {
            if (!config_.workingMap.empty() &&
                snap.mapName != config_.workingMap &&
                config_.autoTravelToWorkingMap) {
                targetMap_ = config_.workingMap;
            }
        }

        if (!targetMap_.empty() && snap.mapName != targetMap_) {
            return 75;
        }

        if (state_ == TravelState::Jumping || state_ == TravelState::WaitingForMap) {
            return 75;
        }

        return 0;
    }

    void onStart() override {
        transitionTo(TravelState::Planning, 0);
        lastPortalDistance_ = std::numeric_limits<double>::max();
        lastProgressTime_ = 0;
        std::cout << "[Travel] Module activated - traveling to " << targetMap_ << "\n";
    }

    void onStop() override {
        if (movement_) {
            movement_->release(name());
        }

        if (state_ == TravelState::Arrived || targetMap_.empty()) {
            transitionTo(TravelState::Idle, 0);
            externalControl_ = false;
            targetMap_.clear();
            currentPath_.clear();
            currentPathIndex_ = 0;
            std::cout << "[Travel] Module deactivated - arrived at destination\n";
        } else {
            std::cout << "[Travel] Module interrupted - will resume later\n";
        }

        publishTelemetry(GameSnapshot{}, "Stopped");
    }

    void tick(const GameSnapshot& snap) override {
        ensureTravelConfig(snap);

        if (!targetMap_.empty() && snap.mapName == targetMap_) {
            transitionTo(TravelState::Arrived, snap.timestampMs);
            std::cout << "[Travel] Arrived at destination: " << targetMap_ << "\n";
            publishTelemetry(snap, "Arrived");
            externalControl_ = false;
            targetMap_.clear();
            currentPath_.clear();
            currentPathIndex_ = 0;
            return;
        }

        switch (state_) {
            case TravelState::Idle:
            case TravelState::Planning:
                handlePlanning(snap);
                break;

            case TravelState::MovingToPortal:
                handleMovingToPortal(snap);
                break;

            case TravelState::AtPortal:
                handleAtPortal(snap);
                break;

            case TravelState::Jumping:
                handleJumping(snap);
                break;

            case TravelState::WaitingForMap:
                handleWaitingForMap(snap);
                break;

            case TravelState::Arrived:
                publishTelemetry(snap, "Arrived");
                break;
        }
    }

    void setDestination(const std::string& mapName) {
        if (mapGraph_.hasMap(mapName) || mapName.empty()) {
            externalControl_ = !mapName.empty();
            targetMap_ = mapName;
            currentPath_.clear();
            currentPathIndex_ = 0;
            transitionTo(mapName.empty() ? TravelState::Idle : TravelState::Planning, 0);

            if (!mapName.empty()) {
                std::cout << "[Travel] New destination set: " << mapName << "\n";
            }
        } else {
            std::cerr << "[Travel] Unknown map: " << mapName << "\n";
        }
    }

    [[nodiscard]] const std::string& getDestination() const noexcept {
        return targetMap_;
    }

    [[nodiscard]] bool isTraveling() const noexcept {
        return !targetMap_.empty() &&
               state_ != TravelState::Idle &&
               state_ != TravelState::Arrived;
    }

    void cancelTravel() {
        externalControl_ = false;
        targetMap_.clear();
        currentPath_.clear();
        currentPathIndex_ = 0;
        transitionTo(TravelState::Idle, 0);
        std::cout << "[Travel] Travel cancelled\n";
    }

    [[nodiscard]] const std::vector<PathStep>& getCurrentPath() const noexcept {
        return currentPath_;
    }

    [[nodiscard]] TravelState getState() const noexcept { return state_; }
    [[nodiscard]] TravelTelemetry getTelemetry() const {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        return telemetry_;
    }

private:
    struct PortalResolveResult {
        Position position;
        int32_t portalId{0};
        bool usingLivePortal{false};
    };

    MapConfig config_;
    MapGraph mapGraph_;
    Navigation navigation_;
    TravelState state_{TravelState::Idle};

    std::string targetMap_;
    bool externalControl_{false};
    std::vector<PathStep> currentPath_;
    int currentPathIndex_{0};

    int64_t lastMoveTime_{0};
    int64_t lastJumpTime_{0};
    int64_t mapChangeWaitStart_{0};
    int64_t lastProgressTime_{0};
    int64_t lastConfigSwitchTime_{0};
    double lastPortalDistance_{std::numeric_limits<double>::max()};
    std::string lastMapName_;
    mutable std::mutex telemetryMutex_;
    TravelTelemetry telemetry_;

    static constexpr int32_t PORTAL_RANGE = 200;
    static constexpr int32_t MOVE_COOLDOWN_MS = 200;
    static constexpr int32_t JUMP_COOLDOWN_MS = 6000;
    static constexpr int32_t MAP_CHANGE_TIMEOUT_MS = 10000;
    static constexpr int32_t PORTAL_STALL_TIMEOUT_MS = 3500;
    static constexpr int32_t CONFIG_SWITCH_COOLDOWN_MS = 1000;

    void transitionTo(TravelState newState, int64_t nowMs) {
        if (state_ == newState) {
            return;
        }
        state_ = newState;
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = newState;
        telemetry_.lastStateChangeMs = nowMs;
    }

    [[nodiscard]] std::unordered_set<std::string> blockedMaps() const {
        std::unordered_set<std::string> blocked(config_.avoidMaps.begin(), config_.avoidMaps.end());
        if (!targetMap_.empty()) {
            blocked.erase(targetMap_);
        }
        return blocked;
    }

    void ensureTravelConfig(const GameSnapshot& snap) {
        if (config_.travelConfigId < 1 || config_.travelConfigId > 2) {
            return;
        }
        if (snap.hero.activeConfig == config_.travelConfigId) {
            return;
        }
        if (snap.timestampMs - lastConfigSwitchTime_ < CONFIG_SWITCH_COOLDOWN_MS) {
            return;
        }
        lastConfigSwitchTime_ = snap.timestampMs;
        engine_->switchConfig(config_.travelConfigId);
    }

    [[nodiscard]] std::optional<PathStep> currentStep() const {
        if (currentPathIndex_ < 0 || currentPathIndex_ >= static_cast<int>(currentPath_.size())) {
            return std::nullopt;
        }
        return currentPath_[currentPathIndex_];
    }

    [[nodiscard]] PortalResolveResult resolvePortalPosition(const GameSnapshot& snap,
                                                            const PathStep& step) const {
        PortalResolveResult result;
        result.position = Position(step.portalX, step.portalY);

        const PortalInfo* exactMatch = nullptr;
        double exactMatchScore = std::numeric_limits<double>::max();
        const PortalInfo* nearestFallback = nullptr;
        double nearestFallbackScore = std::numeric_limits<double>::max();

        for (const auto& portal : snap.entities.portals) {
            if (!portal.isWorldPortal()) continue;

            const double score = Position(portal.x, portal.y).distanceTo(Position(step.portalX, step.portalY));

            if (!portal.targetMapName.empty() && portal.targetMapName == step.toMap) {
                if (score < exactMatchScore) {
                    exactMatchScore = score;
                    exactMatch = &portal;
                }
            }

            if (score < nearestFallbackScore) {
                nearestFallbackScore = score;
                nearestFallback = &portal;
            }
        }

        if (exactMatch) {
            result.position = Position(exactMatch->x, exactMatch->y);
            result.portalId = exactMatch->id;
            result.usingLivePortal = true;
            return result;
        }

        if (nearestFallback && nearestFallbackScore <= 1400.0) {
            result.position = Position(nearestFallback->x, nearestFallback->y);
            result.portalId = nearestFallback->id;
            result.usingLivePortal = true;
        }

        return result;
    }

    void publishTelemetry(const GameSnapshot& snap,
                          std::string_view decision,
                          float portalDistance = 0.0f,
                          bool usingLivePortal = false) {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = state_;
        telemetry_.sourceMap = snap.mapName;
        telemetry_.destinationMap = targetMap_;
        telemetry_.nextMap = (currentPathIndex_ >= 0 &&
                              currentPathIndex_ < static_cast<int>(currentPath_.size()))
            ? currentPath_[currentPathIndex_].toMap
            : std::string{};
        telemetry_.decision = std::string(decision);
        telemetry_.currentHop = currentPathIndex_;
        telemetry_.hopsRemaining = static_cast<int32_t>(currentPath_.size()) - currentPathIndex_;
        telemetry_.portalDistance = portalDistance;
        telemetry_.usingLivePortal = usingLivePortal;
        telemetry_.lastProgressMs = lastProgressTime_;
    }

    void registerPortalProgress(int64_t nowMs, double distance) {
        if (distance + 45.0 < lastPortalDistance_) {
            lastPortalDistance_ = distance;
            lastProgressTime_ = nowMs;
        }
    }

    [[nodiscard]] bool maybeRecoverPortalApproach(const GameSnapshot& snap,
                                                  double distance) {
        registerPortalProgress(snap.timestampMs, distance);
        if (lastProgressTime_ <= 0) {
            lastProgressTime_ = snap.timestampMs;
            return false;
        }

        if (snap.timestampMs - lastProgressTime_ < PORTAL_STALL_TIMEOUT_MS) {
            return false;
        }

        if (movement_) {
            movement_->release(name());
        }

        transitionTo(TravelState::Planning, snap.timestampMs);
        lastProgressTime_ = snap.timestampMs;
        lastPortalDistance_ = std::numeric_limits<double>::max();
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_.replans++;
            telemetry_.recoveries++;
        }
        std::cout << "[Travel] Portal approach stalled, replanning\n";
        publishTelemetry(snap, "StuckReplan", static_cast<float>(distance));
        return true;
    }

    void handlePlanning(const GameSnapshot& snap) {
        if (targetMap_.empty()) {
            transitionTo(TravelState::Idle, snap.timestampMs);
            publishTelemetry(snap, "Idle");
            return;
        }

        currentPath_ = mapGraph_.findPath(snap.mapName, targetMap_, blockedMaps());
        currentPathIndex_ = 0;

        if (currentPath_.empty()) {
            std::cerr << "[Travel] No path found from " << snap.mapName
                      << " to " << targetMap_ << "\n";
            targetMap_.clear();
            transitionTo(TravelState::Idle, snap.timestampMs);
            publishTelemetry(snap, "NoPath");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_.replans++;
        }

        std::cout << "[Travel] Path found: " << currentPath_.size() << " jumps\n";
        for (const auto& step : currentPath_) {
            std::cout << "  " << step.fromMap << " -> " << step.toMap
                      << " via (" << step.portalX << ", " << step.portalY << ")\n";
        }

        transitionTo(TravelState::MovingToPortal, snap.timestampMs);
        lastMapName_ = snap.mapName;
        lastPortalDistance_ = std::numeric_limits<double>::max();
        lastProgressTime_ = snap.timestampMs;
        publishTelemetry(snap, "Planning");
    }

    void handleMovingToPortal(const GameSnapshot& snap) {
        if (snap.mapName != lastMapName_) {
            std::cout << "[Travel] Map changed unexpectedly, replanning...\n";
            transitionTo(TravelState::Planning, snap.timestampMs);
            publishTelemetry(snap, "MapChangedReplan");
            return;
        }

        auto stepOpt = currentStep();
        if (!stepOpt.has_value()) {
            transitionTo(TravelState::Planning, snap.timestampMs);
            publishTelemetry(snap, "PathExhausted");
            return;
        }

        const auto resolved = resolvePortalPosition(snap, *stepOpt);
        const Position heroPos(snap.hero.x, snap.hero.y);
        const double dist = heroPos.distanceTo(resolved.position);

        if (dist <= PORTAL_RANGE) {
            transitionTo(TravelState::AtPortal, snap.timestampMs);
            lastPortalDistance_ = dist;
            lastProgressTime_ = snap.timestampMs;
            std::cout << "[Travel] Arrived at portal to " << stepOpt->toMap << "\n";
            publishTelemetry(snap, "AtPortal", static_cast<float>(dist), resolved.usingLivePortal);
            return;
        }

        if (maybeRecoverPortalApproach(snap, dist)) {
            return;
        }

        if (snap.timestampMs - lastMoveTime_ >= MOVE_COOLDOWN_MS) {
            lastMoveTime_ = snap.timestampMs;
            if (movement_) {
                movement_->move(name(), snap, resolved.position, MoveIntent::Travel);
            } else {
                engine_->moveTo(static_cast<float>(resolved.position.x),
                                static_cast<float>(resolved.position.y));
            }
        }

        publishTelemetry(
            snap,
            resolved.usingLivePortal ? "MovingLivePortal" : "MovingGraphPortal",
            static_cast<float>(dist),
            resolved.usingLivePortal
        );
    }

    void handleAtPortal(const GameSnapshot& snap) {
        const auto now = snap.timestampMs;
        if (now - lastJumpTime_ < JUMP_COOLDOWN_MS) {
            publishTelemetry(snap, "JumpCooldown");
            return;
        }

        lastJumpTime_ = now;
        std::cout << "[Travel] Jumping through portal...\n";
        engine_->teleport();

        transitionTo(TravelState::Jumping, now);
        mapChangeWaitStart_ = now;
        publishTelemetry(snap, "Teleport");
    }

    void handleJumping(const GameSnapshot& snap) {
        const auto now = snap.timestampMs;

        if (snap.mapName != lastMapName_) {
            std::cout << "[Travel] Map changed to " << snap.mapName << "\n";
            lastMapName_ = snap.mapName;
            currentPathIndex_++;
            transitionTo(TravelState::WaitingForMap, now);
            mapChangeWaitStart_ = now;
            publishTelemetry(snap, "MapChanged");
            return;
        }

        if (now - mapChangeWaitStart_ > MAP_CHANGE_TIMEOUT_MS) {
            std::cout << "[Travel] Teleport timeout, retrying...\n";
            transitionTo(TravelState::AtPortal, now);
            {
                std::lock_guard<std::mutex> lock(telemetryMutex_);
                telemetry_.recoveries++;
            }
            publishTelemetry(snap, "TeleportTimeout");
            return;
        }

        publishTelemetry(snap, "Jumping");
    }

    void handleWaitingForMap(const GameSnapshot& snap) {
        const auto now = snap.timestampMs;
        if (now - mapChangeWaitStart_ < 2000) {
            publishTelemetry(snap, "MapLoadWait");
            return;
        }

        if (snap.mapName == targetMap_) {
            transitionTo(TravelState::Arrived, now);
            std::cout << "[Travel] Arrived at destination: " << targetMap_ << "\n";
            publishTelemetry(snap, "Arrived");
            return;
        }

        if (currentPathIndex_ < static_cast<int>(currentPath_.size())) {
            transitionTo(TravelState::MovingToPortal, now);
            lastPortalDistance_ = std::numeric_limits<double>::max();
            lastProgressTime_ = now;
            publishTelemetry(snap, "ContinueRoute");
        } else {
            std::cout << "[Travel] Path exhausted, replanning...\n";
            transitionTo(TravelState::Planning, now);
            publishTelemetry(snap, "PathExhausted");
        }
    }
};

} // namespace dynamo
