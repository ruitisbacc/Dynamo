#pragma once

/**
 * @file combat_module.hpp
 * @brief Combat module - handles NPC targeting and killing
 * 
 * Manages NPC target selection, approach, and attack sequences.
 */

#include "module.hpp"
#include "bot_config.hpp"
#include "collection_helpers.hpp"
#include "npc_database.hpp"
#include "navigation.hpp"
#include "movement_controller.hpp"
#include "../game/game_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <random>
#include <regex>
#include <optional>
#include <unordered_map>

namespace dynamo {

/**
 * @brief Combat state machine states
 */
enum class CombatState {
    Searching,      // Looking for target
    Approaching,    // Moving towards target
    Attacking,      // Engaging target
    AwaitingKill,   // Waiting for target to die
    Cooldown        // Brief pause between kills
};

struct CombatTelemetry {
    CombatState state{CombatState::Searching};
    CombatMovementMode movementMode{CombatMovementMode::Adaptive};
    std::string movementDecision{"Idle"};
    std::string recoveryReason;
    std::string targetName;
    int32_t targetId{0};
    float targetDistance{0.0f};
    bool stuckRecoveryActive{false};
    int32_t stuckRecoveries{0};
    int64_t lastModeChangeMs{0};
    int64_t lastRecoveryMs{0};
    int64_t lastProgressMs{0};
};

struct CombatTargetSelection {
    const ShipInfo* ship{nullptr};
    const NpcTargetConfig* config{nullptr};
    double distance{0.0};
    double durabilityRatio{1.0};
    double score{0.0};
};

struct CombatTargetLockout {
    int32_t failures{0};
    int64_t untilMs{0};
};

struct CombatCollectSelection {
    const BoxInfo* box{nullptr};
    Position approachPosition;
};

struct CombatTargetMotion {
    Position current;
    Position destination;
    Position direction;
    Position anchor;
    double speed{0.0};
    double pathRemaining{0.0};
    double leadTime{0.0};
    bool moving{false};
};

struct OrbitSolution {
    Position point;
    Position anchor;
    double score{-std::numeric_limits<double>::infinity()};
    double rangeError{std::numeric_limits<double>::infinity()};
    double edgeClearance{0.0};
    bool clockwise{true};
    bool valid{false};
};

struct OrbitScoringContext {
    Position heroPos;
    Position targetPos;
    Position anchor;
    double desiredRange{0.0};
    double correctiveRadius{0.0};
    double currentAngle{0.0};
    std::optional<Position> collectApproachPoint;
};

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
    CombatModule(std::shared_ptr<GameEngine> engine,
                 std::shared_ptr<MovementController> movement,
                 const CombatConfig& config,
                 const CollectConfig& collectConfig = CollectConfig{})
        : Module(std::move(engine), std::move(movement))
        , config_(config)
        , collectConfig_(collectConfig) {
        compileTargetPatterns();
    }
    
    [[nodiscard]] std::string_view name() const noexcept override {
        return "Combat";
    }
    
    [[nodiscard]] int getPriority(const GameSnapshot& snap) override {
        if (!config_.enabled || !enabled_) {
            return 0;
        }

        if (currentTargetId_ != 0 &&
            (state_ == CombatState::Approaching ||
             state_ == CombatState::Attacking ||
             state_ == CombatState::AwaitingKill)) {
            return config_.priority;
        }

        return findBestTarget(snap).has_value() ? config_.priority : 0;
    }
    
    void onStart() override {
        state_ = CombatState::Searching;
        currentTargetId_ = 0;
        currentMovementMode_ = CombatMovementMode::Adaptive;
        mixedUseKite_ = false;
        mixedMovementSwitchAtMs_ = 0;
        autoRocketEnabled_ = false;
        autoRocketStateKnown_ = false;
        lastAutoRocketToggleTime_ = 0;
        trackedTargetId_ = 0;
        lastTrackedTargetDistance_ = std::numeric_limits<double>::max();
        lastTrackedHeroPos_ = Position{};
        lastProgressTime_ = 0;
        currentTargetAcquiredAtMs_ = 0;
        lastDamageProgressAtMs_ = 0;
        lastCombatCollectTime_ = 0;
        combatCollectBlockedUntilMs_ = 0;
        recoveryUntilMs_ = 0;
        lastRecoveryAtMs_ = 0;
        lastOrbitDirectionChangeAtMs_ = 0;
        currentTargetDurability_ = 0;
        currentTargetRecoveries_ = 0;
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_ = CombatTelemetry{};
            telemetry_.movementMode = currentMovementMode_;
            telemetry_.lastModeChangeMs = 0;
        }
        std::cout << "[Combat] Module activated - searching for targets\n";
    }
    
    void onStop() override {
        stopCombatActions(true, true);
        state_ = CombatState::Searching;
        currentTargetId_ = 0;
        currentMovementMode_ = CombatMovementMode::Adaptive;
        mixedUseKite_ = false;
        mixedMovementSwitchAtMs_ = 0;
        autoRocketEnabled_ = false;
        autoRocketStateKnown_ = false;
        lastAutoRocketToggleTime_ = 0;
        trackedTargetId_ = 0;
        lastTrackedTargetDistance_ = std::numeric_limits<double>::max();
        currentTargetAcquiredAtMs_ = 0;
        lastDamageProgressAtMs_ = 0;
        lastCombatCollectTime_ = 0;
        combatCollectBlockedUntilMs_ = 0;
        recoveryUntilMs_ = 0;
        lastOrbitDirectionChangeAtMs_ = 0;
        currentTargetDurability_ = 0;
        currentTargetRecoveries_ = 0;
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_.state = CombatState::Searching;
            telemetry_.movementDecision = "Stopped";
            telemetry_.targetId = 0;
            telemetry_.targetName.clear();
            telemetry_.targetDistance = 0.0f;
            telemetry_.stuckRecoveryActive = false;
        }
        std::cout << "[Combat] Module deactivated\n";
    }
    
    void tick(const GameSnapshot& snap) override {
        ensureCombatConfig(snap);
        pruneTargetLockouts(snap.timestampMs);

        switch (state_) {
            case CombatState::Searching:
                handleSearching(snap);
                break;
                
            case CombatState::Approaching:
                handleApproaching(snap);
                break;
                
            case CombatState::Attacking:
                handleAttacking(snap);
                break;
                
            case CombatState::AwaitingKill:
                handleAwaitingKill(snap);
                break;
                
            case CombatState::Cooldown:
                handleCooldown(snap);
                break;
        }
        
        // Anti-ban random movement
        if (config_.randomMovement &&
            state_ == CombatState::AwaitingKill &&
            recoveryUntilMs_ <= snap.timestampMs) {
            handleAntibanMovement(snap);
        }
    }
    
    [[nodiscard]] CombatState getState() const noexcept { return state_; }
    [[nodiscard]] int32_t getCurrentTargetId() const noexcept { return currentTargetId_; }
    [[nodiscard]] CombatTelemetry getTelemetry() const {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        return telemetry_;
    }

private:
    CombatConfig config_;
    CollectConfig collectConfig_;
    Navigation navigation_;
    CombatState state_{CombatState::Searching};
    
    int32_t currentTargetId_{0};
    int32_t currentAmmoType_{1};
    int32_t currentRocketType_{0};
    CombatMovementMode currentMovementMode_{CombatMovementMode::Adaptive};
    int64_t lastSelectTime_{0};
    int64_t lastAttackTime_{0};
    int64_t lastRocketSwitchTime_{0};
    int64_t lastAutoRocketToggleTime_{0};
    int64_t lastMoveTime_{0};
    int64_t lastConfigSwitchTime_{0};
    int64_t lastAntibanMoveTime_{0};
    int64_t killStartTime_{0};
    int64_t lastProgressTime_{0};
    int64_t currentTargetAcquiredAtMs_{0};
    int64_t lastDamageProgressAtMs_{0};
    int64_t recoveryUntilMs_{0};
    int64_t lastRecoveryAtMs_{0};
    int64_t lastOrbitDirectionChangeAtMs_{0};
    int64_t mixedMovementSwitchAtMs_{0};
    int64_t combatCollectBlockedUntilMs_{0};
    bool mixedUseKite_{false};
    bool autoRocketEnabled_{false};
    bool autoRocketStateKnown_{false};
    
    bool orbitClockwise_{true};
    int32_t trackedTargetId_{0};
    int32_t currentTargetDurability_{0};
    int32_t currentTargetRecoveries_{0};
    double lastTrackedTargetDistance_{std::numeric_limits<double>::max()};
    Position lastTrackedHeroPos_;
    Position recoveryMoveTarget_;
    mutable std::mutex telemetryMutex_;
    CombatTelemetry telemetry_;
    std::unordered_map<int32_t, CombatTargetLockout> targetLockouts_;
    int64_t lastCombatCollectTime_{0};
    static constexpr double COMBAT_COLLECT_RANGE = 200.0;

    std::vector<std::regex> targetPatterns_;
    static constexpr int64_t CONFIG_SWITCH_COOLDOWN_MS = 1000;
    static constexpr int64_t AUTO_ROCKET_TOGGLE_COOLDOWN_MS = 350;
    static constexpr int64_t TARGET_LOCKOUT_MS = 10000;
    static constexpr int64_t APPROACH_TIMEOUT_MS = 18000;
    static constexpr int64_t DAMAGE_STALL_TIMEOUT_MS = 8000;
    static constexpr int64_t ORBIT_DIRECTION_SWITCH_COOLDOWN_MS = 650;
    static constexpr double TARGET_LEASH_DISTANCE = 450.0;
    static constexpr double LOW_HP_FOLLOW_RANGE_SCALE = 0.8;
    static constexpr double ATTACK_READY_MARGIN = 12.0;
    static constexpr int32_t MAX_TARGET_RECOVERIES = 2;

    [[nodiscard]] MapBounds currentBounds() const {
        MapBounds bounds;
        const auto info = engine_->mapInfo();
        if (info.width > 0) {
            bounds.maxX = static_cast<double>(info.width);
        }
        if (info.height > 0) {
            bounds.maxY = static_cast<double>(info.height);
        }
        return bounds;
    }

    [[nodiscard]] double effectiveMaxDistance(const NpcTargetConfig& target) const {
        const double globalMaxDistance = config_.maxCombatDistance > 0
            ? static_cast<double>(config_.maxCombatDistance)
            : 0.0;
        const double perTargetMaxDistance = target.maxDistance > 0
            ? static_cast<double>(target.maxDistance)
            : 0.0;

        if (globalMaxDistance > 0.0 && perTargetMaxDistance > 0.0) {
            return std::min(globalMaxDistance, perTargetMaxDistance);
        }
        return globalMaxDistance > 0.0 ? globalMaxDistance : perTargetMaxDistance;
    }

    void moveWithinBounds(const GameSnapshot& snap,
                          const Position& requestedTarget,
                          MoveIntent intent) {
        const Position clampedTarget = currentBounds().clamp(requestedTarget, 220.0);
        if (movement_) {
            movement_->move(name(), snap, clampedTarget, intent);
        } else {
            engine_->moveTo(static_cast<float>(clampedTarget.x),
                            static_cast<float>(clampedTarget.y));
        }
    }

    void stopCombatActions(bool releaseMovement, bool clearSelection) {
        if (currentTargetId_ != 0 || state_ == CombatState::Attacking || state_ == CombatState::AwaitingKill) {
            engine_->stopAttack();
        }
        disableAutoRocket(0, true);
        if (clearSelection && currentTargetId_ != 0) {
            engine_->lockTarget(0);
        }
        if (releaseMovement && movement_) {
            movement_->release(name());
        }
    }

    void pruneTargetLockouts(int64_t now) {
        for (auto it = targetLockouts_.begin(); it != targetLockouts_.end();) {
            if (it->second.untilMs <= now) {
                it = targetLockouts_.erase(it);
            } else {
                ++it;
            }
        }
    }

    [[nodiscard]] bool isTargetLockedOut(int32_t targetId, int64_t now) const {
        auto it = targetLockouts_.find(targetId);
        return it != targetLockouts_.end() && it->second.untilMs > now;
    }

    void applyTargetLockout(int32_t targetId,
                            int64_t now,
                            std::string_view reason,
                            int64_t lockoutMs = TARGET_LOCKOUT_MS) {
        if (targetId == 0) {
            return;
        }

        auto& lockout = targetLockouts_[targetId];
        ++lockout.failures;
        const int64_t duration = lockoutMs + static_cast<int64_t>(lockout.failures - 1) * 2500;
        lockout.untilMs = std::max(lockout.untilMs, now + duration);

        std::cout << "[Combat] Locking out target " << targetId
                  << " for " << (duration / 1000.0) << "s"
                  << " (" << reason << ")\n";
    }

    void clearCurrentTargetState() {
        currentTargetId_ = 0;
        currentAmmoType_ = 1;
        currentRocketType_ = 0;
        currentMovementMode_ = CombatMovementMode::Adaptive;
        trackedTargetId_ = 0;
        currentTargetDurability_ = 0;
        currentTargetRecoveries_ = 0;
        lastTrackedTargetDistance_ = std::numeric_limits<double>::max();
        currentTargetAcquiredAtMs_ = 0;
        lastProgressTime_ = 0;
        lastDamageProgressAtMs_ = 0;
        recoveryUntilMs_ = 0;
        lastRecoveryAtMs_ = 0;
        lastOrbitDirectionChangeAtMs_ = 0;
        mixedUseKite_ = false;
        mixedMovementSwitchAtMs_ = 0;
        orbitClockwise_ = true;
    }

    [[nodiscard]] double targetSelectionRange() const {
        const double attackEnvelope = static_cast<double>(config_.attackRange) + 140.0;
        const double followEnvelope = static_cast<double>(config_.followDistance) + 200.0;
        return std::max(750.0, std::max(attackEnvelope, followEnvelope));
    }

    void ensureCombatConfig(const GameSnapshot& snap) {
        if (config_.configId < 1 || config_.configId > 2) {
            return;
        }
        if (snap.hero.activeConfig == config_.configId) {
            return;
        }
        if (snap.timestampMs - lastConfigSwitchTime_ < CONFIG_SWITCH_COOLDOWN_MS) {
            return;
        }
        lastConfigSwitchTime_ = snap.timestampMs;
        engine_->switchConfig(config_.configId);
    }

    void compileTargetPatterns() {
        targetPatterns_.clear();
        for (const auto& target : config_.targets) {
            try {
                targetPatterns_.emplace_back(target.namePattern, std::regex::icase);
            } catch (const std::regex_error&) {
                std::cerr << "[Combat] Invalid regex pattern: " << target.namePattern << "\n";
                // Fallback to exact match
                targetPatterns_.emplace_back(regex_escape(target.namePattern), std::regex::icase);
            }
        }
    }
    
    static std::string regex_escape(const std::string& s) {
        static const std::regex special_chars{R"([-[\]{}()*+?.,\^$|#\s])"};
        return std::regex_replace(s, special_chars, R"(\$&)");
    }

    [[nodiscard]] static bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (std::size_t i = 0; i < lhs.size(); ++i) {
            const auto left = static_cast<unsigned char>(lhs[i]);
            const auto right = static_cast<unsigned char>(rhs[i]);
            if (std::tolower(left) != std::tolower(right)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static std::string_view movementModeName(CombatMovementMode mode) {
        switch (mode) {
            case CombatMovementMode::Adaptive: return "Adaptive";
            case CombatMovementMode::Direct:
            case CombatMovementMode::Orbit:
            case CombatMovementMode::Kite:
            case CombatMovementMode::Mixed:
            case CombatMovementMode::Default:
            default:
                return "Adaptive";
        }
    }

    [[nodiscard]] static double wrapAngle(double angle) {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kTwoPi = kPi * 2.0;
        while (angle <= -kPi) {
            angle += kTwoPi;
        }
        while (angle > kPi) {
            angle -= kTwoPi;
        }
        return angle;
    }

    [[nodiscard]] static Position add(const Position& lhs, const Position& rhs) {
        return Position(lhs.x + rhs.x, lhs.y + rhs.y);
    }

    [[nodiscard]] static Position subtract(const Position& lhs, const Position& rhs) {
        return Position(lhs.x - rhs.x, lhs.y - rhs.y);
    }

    [[nodiscard]] static Position scale(const Position& value, double factor) {
        return Position(value.x * factor, value.y * factor);
    }

    [[nodiscard]] static double vectorLength(const Position& value) {
        return std::sqrt(value.x * value.x + value.y * value.y);
    }

    [[nodiscard]] static Position normalizedOr(const Position& value, const Position& fallback) {
        const double length = vectorLength(value);
        if (length < 1e-6) {
            return fallback;
        }
        return scale(value, 1.0 / length);
    }

    [[nodiscard]] static double dot(const Position& lhs, const Position& rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y;
    }

    [[nodiscard]] static Position tangentFromRadial(const Position& radial, bool clockwise) {
        return clockwise
            ? Position(-radial.y, radial.x)
            : Position(radial.y, -radial.x);
    }

    [[nodiscard]] static double distanceToSegment(const Position& point,
                                                  const Position& segmentStart,
                                                  const Position& segmentEnd) {
        const Position segment = subtract(segmentEnd, segmentStart);
        const double segmentLengthSquared = segment.x * segment.x + segment.y * segment.y;
        if (segmentLengthSquared < 1e-6) {
            return point.distanceTo(segmentStart);
        }

        const Position pointVector = subtract(point, segmentStart);
        const double factor = std::clamp(
            dot(pointVector, segment) / segmentLengthSquared,
            0.0,
            1.0
        );
        const Position projection = add(segmentStart, scale(segment, factor));
        return point.distanceTo(projection);
    }

    [[nodiscard]] static double rangeTolerance(double desiredRange) {
        return std::max(18.0, desiredRange * 0.035);
    }

    [[nodiscard]] static double rangeSlack(double desiredRange) {
        return std::max(40.0, desiredRange * 0.07);
    }

    [[nodiscard]] static double correctiveOrbitRadius(double currentDistance,
                                                      double desiredRange) {
        // DarkBot-style: 100% of distance error, capped at ±50% of range
        const double radiusError = currentDistance - desiredRange;
        const double correction = std::clamp(
            radiusError,
            -desiredRange * 0.5,
            desiredRange * 0.5
        );
        return desiredRange - correction;
    }

    [[nodiscard]] CombatTargetMotion buildTargetMotion(const Position& /*heroPos*/,
                                                       const ShipInfo& target,
                                                       double /*heroSpeed*/,
                                                       double /*desiredRange*/) const {
        CombatTargetMotion motion;
        motion.current = Position(target.x, target.y);
        motion.anchor = motion.current;

        // Determine effective NPC speed: prefer observed, fallback to server, then default
        const double effectiveNpcSpeed = target.observedSpeedEma > 10.0f
            ? std::clamp(static_cast<double>(target.observedSpeedEma), 90.0, 520.0)
            : (target.speed > 0
                ? std::clamp(static_cast<double>(target.speed), 90.0, 520.0)
                : 220.0);

        // Determine if NPC is actually moving (observed beats server flag)
        const bool observedMoving = target.observedSpeedEma > 15.0f;
        const bool serverMoving = target.isMoving &&
            (target.targetX != 0 || target.targetY != 0);
        if (!observedMoving && !serverMoving) {
            return motion;
        }

        // Direction: prefer observed velocity, fall back to targetX/Y path vector
        const double observedVLen = std::sqrt(
            static_cast<double>(target.observedVx) * target.observedVx +
            static_cast<double>(target.observedVy) * target.observedVy);
        Position moveDir;
        if (observedVLen > 10.0) {
            moveDir = Position(target.observedVx / observedVLen,
                               target.observedVy / observedVLen);
            // Compute remaining path from server target if available
            if (target.isMoving && (target.targetX != 0 || target.targetY != 0)) {
                motion.destination = Position(target.targetX, target.targetY);
                motion.pathRemaining = vectorLength(subtract(motion.destination, motion.current));
            }
        } else {
            motion.destination = Position(target.targetX, target.targetY);
            const Position pathVector = subtract(motion.destination, motion.current);
            const double pathLen = vectorLength(pathVector);
            if (pathLen < 12.0) {
                return motion;
            }
            moveDir = scale(pathVector, 1.0 / pathLen);
            motion.pathRemaining = pathLen;
        }

        motion.direction = moveDir;
        motion.moving = true;
        motion.speed = effectiveNpcSpeed;

        // Fixed 400ms prediction, capped by remaining path to avoid overshoot
        constexpr double kPredictionMs = 400.0;
        double leadDistance = effectiveNpcSpeed * (kPredictionMs / 1000.0);
        if (motion.pathRemaining > 0.0) {
            leadDistance = std::min(leadDistance, motion.pathRemaining);
        }
        motion.anchor = add(motion.current, scale(moveDir, leadDistance));
        motion.leadTime = kPredictionMs / 1000.0;
        return motion;
    }

    [[nodiscard]] Position predictedCombatTargetPosition(const Position& heroPos,
                                                         const ShipInfo& target,
                                                         double heroSpeed,
                                                         double desiredRange) const {
        return buildTargetMotion(heroPos, target, heroSpeed, desiredRange).anchor;
    }

    [[nodiscard]] double orbitNpcAvoidanceRadius(const ShipInfo& ship,
                                                 const GameSnapshot& snap,
                                                 double combatRange) const {
        double radius = std::clamp(combatRange * 0.55, 260.0, 520.0);
        if (ship.selectedTarget == snap.hero.id && ship.isAttacking) {
            radius += 120.0;
        }
        if (ship.isMoving) {
            radius += 40.0;
        }
        return radius;
    }

    [[nodiscard]] double scoreOrbitCandidate(const Position& candidate,
                                             const GameSnapshot& snap,
                                             const ShipInfo& target,
                                             const OrbitScoringContext& context,
                                             bool clockwise,
                                             bool preferredClockwise) const {
        const MapBounds bounds = currentBounds();
        if (!bounds.contains(candidate, 220.0)) {
            return -std::numeric_limits<double>::infinity();
        }

        const double distanceToAnchor = candidate.distanceTo(context.anchor);
        const double distanceToTarget = candidate.distanceTo(context.targetPos);
        const double desiredRangeError = std::abs(distanceToAnchor - context.desiredRange);
        const double correctiveError = std::abs(distanceToAnchor - context.correctiveRadius);
        const double travelDistance = context.heroPos.distanceTo(candidate);

        double score = 0.0;
        score -= desiredRangeError * 28.0;
        score -= correctiveError * 10.0;
        score -= std::abs(distanceToTarget - context.desiredRange) * 3.0;
        score -= travelDistance * 0.15;

        const double candidateAngle = std::atan2(candidate.y - context.anchor.y, candidate.x - context.anchor.x);
        const double delta = wrapAngle(candidateAngle - context.currentAngle);
        const double orientedDelta = clockwise ? -delta : delta;

        if (orientedDelta < 0.035) {
            score -= 1800.0 + std::abs(orientedDelta) * 550.0;
        } else {
            score += std::min(orientedDelta, 0.75) * 1450.0;
        }

        if (clockwise != preferredClockwise) {
            score -= 180.0;
        }

        const double edgeClearance = std::min({
            candidate.x - bounds.minX,
            bounds.maxX - candidate.x,
            candidate.y - bounds.minY,
            bounds.maxY - candidate.y
        });
        if (edgeClearance < 260.0) {
            score -= (260.0 - edgeClearance) * 18.0;
        } else {
            score += std::clamp(edgeClearance - 260.0, 0.0, 900.0) * 0.28;
        }

        if (context.collectApproachPoint.has_value()) {
            const double candidateToBox = candidate.distanceTo(*context.collectApproachPoint);
            const double heroToBox = context.heroPos.distanceTo(*context.collectApproachPoint);

            if (candidateToBox <= 240.0) {
                score += 1800.0;
            } else if (candidateToBox <= 620.0) {
                score += (620.0 - candidateToBox) * 3.4;
            }

            if (candidateToBox + 60.0 < heroToBox) {
                score += 260.0;
            }
        }

        for (const auto& ship : snap.entities.ships) {
            if (ship.id == 0 || ship.id == target.id || !ship.isNpc || ship.isDestroyed || ship.isCloaked) {
                continue;
            }

            const double hazardRadius = orbitNpcAvoidanceRadius(ship, snap, context.desiredRange);
            const Position shipPos(ship.x, ship.y);
            const double distanceToNpc = candidate.distanceTo(shipPos);
            const double segmentDistance = distanceToSegment(shipPos, context.heroPos, candidate);
            if (distanceToNpc < hazardRadius * 0.55) {
                score -= 15000.0 + (hazardRadius - distanceToNpc) * 50.0;
                continue;
            }
            if (distanceToNpc < hazardRadius) {
                score -= (hazardRadius - distanceToNpc) * 20.0;
            }
            if (segmentDistance < hazardRadius * 0.7) {
                score -= (hazardRadius * 0.7 - segmentDistance) * 14.0;
            }
        }

        return score;
    }

    [[nodiscard]] OrbitSolution evaluateOrbitDirection(const GameSnapshot& snap,
                                                       const ShipInfo& target,
                                                       const OrbitScoringContext& context,
                                                       double heroSpeed,
                                                       double npcSpeed,
                                                       bool clockwise,
                                                       bool preferredClockwise) const {
        OrbitSolution best;
        best.anchor = context.anchor;
        best.clockwise = clockwise;

        const Position fallback = navigation_.continuousOrbit(
            context.heroPos,
            context.anchor,
            context.correctiveRadius,
            heroSpeed,
            npcSpeed,
            clockwise
        );
        const double fallbackScore = scoreOrbitCandidate(
            fallback,
            snap,
            target,
            context,
            clockwise,
            preferredClockwise
        );
        best.point = fallback;
        best.score = fallbackScore;
        best.rangeError = std::abs(fallback.distanceTo(context.anchor) - context.desiredRange);
        best.edgeClearance = std::min({
            fallback.x - currentBounds().minX,
            currentBounds().maxX - fallback.x,
            fallback.y - currentBounds().minY,
            currentBounds().maxY - fallback.y
        });
        best.valid = std::isfinite(fallbackScore);

        const double currentDistanceToAnchor = context.heroPos.distanceTo(context.anchor);
        const double radialCorrectionDist =
            std::abs(currentDistanceToAnchor - context.desiredRange);
        const double linearBudget =
            heroSpeed * 0.72 + std::min(npcSpeed, 240.0) * 0.38;
        const double tangentialBudget = std::max(linearBudget - radialCorrectionDist, 0.0);
        const double angleStep = std::clamp(
            tangentialBudget / std::max(context.desiredRange, 100.0),
            0.03,
            0.34
        );
        const double scanStep = std::max(0.03, angleStep * 0.45);
        const double primaryAngle = context.currentAngle + (clockwise ? -angleStep : angleStep);

        // Chord compensation: the hero moves in a straight line to the orbit point,
        // cutting inside the circle. Inflate the orbit radius to counteract.
        const double halfAngle = std::min(angleStep * 0.5, 0.25);
        const double chordCompensation = context.desiredRange * (1.0 - std::cos(halfAngle));

        // Fine-grained radius offsets: dense near desired range, sparser further out
        const std::array<double, 9> radiusOffsets{
            -0.12, -0.06, -0.03, -0.01, 0.0, 0.01, 0.03, 0.06, 0.12
        };

        for (double radiusOffset : radiusOffsets) {
            const double radius = std::clamp(
                context.correctiveRadius + chordCompensation +
                    context.desiredRange * radiusOffset,
                context.desiredRange * 0.72,
                context.desiredRange * 1.28
            );

            for (int angleOffset = -6; angleOffset <= 6; ++angleOffset) {
                const double angle = primaryAngle + static_cast<double>(angleOffset) * scanStep;
                const Position candidate(
                    context.anchor.x + std::cos(angle) * radius,
                    context.anchor.y + std::sin(angle) * radius
                );

                const double score = scoreOrbitCandidate(
                    candidate,
                    snap,
                    target,
                    context,
                    clockwise,
                    preferredClockwise
                );

                if (!std::isfinite(score) || score <= best.score) {
                    continue;
                }

                best.point = candidate;
                best.score = score;
                best.rangeError = std::abs(candidate.distanceTo(context.anchor) - context.desiredRange);
                best.edgeClearance = std::min({
                    candidate.x - currentBounds().minX,
                    currentBounds().maxX - candidate.x,
                    candidate.y - currentBounds().minY,
                    currentBounds().maxY - candidate.y
                });
                best.valid = true;
            }
        }

        return best;
    }

    [[nodiscard]] OrbitSolution selectOrbitSolution(const GameSnapshot& snap,
                                                    const ShipInfo& target,
                                                    double combatRange,
                                                    double heroSpeed,
                                                    double npcSpeed,
                                                    bool preferredClockwise,
                                                    const BoxInfo* collectBox = nullptr) const {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const CombatTargetMotion motion = buildTargetMotion(heroPos, target, heroSpeed, combatRange);
        const double currentDistanceToAnchor = heroPos.distanceTo(motion.anchor);

        OrbitScoringContext context;
        context.heroPos = heroPos;
        context.targetPos = Position(target.x, target.y);
        context.anchor = motion.anchor;
        context.desiredRange = combatRange;
        context.correctiveRadius = correctiveOrbitRadius(currentDistanceToAnchor, combatRange);
        context.currentAngle = std::atan2(heroPos.y - motion.anchor.y, heroPos.x - motion.anchor.x);
        if (collectBox && collectBox->existsOnMap) {
            context.collectApproachPoint = collectApproachPosition(*collectBox);
        }

        const OrbitSolution preferred = evaluateOrbitDirection(
            snap,
            target,
            context,
            heroSpeed,
            npcSpeed,
            preferredClockwise,
            preferredClockwise
        );
        const OrbitSolution alternate = evaluateOrbitDirection(
            snap,
            target,
            context,
            heroSpeed,
            npcSpeed,
            !preferredClockwise,
            preferredClockwise
        );

        if (!preferred.valid) {
            return alternate;
        }
        if (!alternate.valid) {
            return preferred;
        }

        const bool switchAllowed =
            snap.timestampMs - lastOrbitDirectionChangeAtMs_ >= ORBIT_DIRECTION_SWITCH_COOLDOWN_MS;
        const bool alternateClearlyBetter = alternate.score > preferred.score + 220.0;
        const bool alternateFixesRange =
            alternate.rangeError + rangeTolerance(context.desiredRange) * 0.8 < preferred.rangeError;
        const bool preferredCramped = preferred.edgeClearance < 260.0;
        const bool alternateLessCramped = alternate.edgeClearance > preferred.edgeClearance + 80.0;

        if ((alternateClearlyBetter && switchAllowed) ||
            ((alternateFixesRange || alternateLessCramped) &&
             alternate.score > preferred.score - 120.0 &&
             (switchAllowed || preferredCramped))) {
            return alternate;
        }

        return preferred;
    }

    [[nodiscard]] OrbitSolution selectApproachEntrySolution(const GameSnapshot& snap,
                                                            const ShipInfo& target,
                                                            double desiredRange,
                                                            double heroSpeed,
                                                            double npcSpeed,
                                                            bool preferredClockwise) const {
        OrbitSolution solution;
        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position anchor = predictedCombatTargetPosition(heroPos, target, heroSpeed, desiredRange);
        const double anchorDistance = heroPos.distanceTo(anchor);
        solution.anchor = anchor;
        solution.clockwise = preferredClockwise;
        solution.valid = true;
        solution.rangeError = std::abs(anchorDistance - desiredRange);

        const Position directApproach = navigation_.approachPosition(heroPos, anchor, desiredRange);
        solution.point = directApproach;

        if (anchorDistance > desiredRange + std::max(260.0, desiredRange * 0.55)) {
            return solution;
        }

        const OrbitSolution orbitEntry = selectOrbitSolution(
            snap,
            target,
            desiredRange,
            heroSpeed,
            npcSpeed,
            preferredClockwise
        );
        if (!orbitEntry.valid) {
            return solution;
        }

        const double blend = std::clamp(
            1.0 - ((anchorDistance - desiredRange) / std::max(240.0, desiredRange * 0.45)),
            0.0,
            1.0
        );
        const double smoothBlend = blend * blend * (3.0 - 2.0 * blend);
        solution.point = directApproach.interpolate(orbitEntry.point, smoothBlend);
        solution.clockwise = orbitEntry.clockwise;
        solution.score = orbitEntry.score;
        solution.rangeError = orbitEntry.rangeError;
        return solution;
    }

    [[nodiscard]] Position selectCreateSpacePosition(const Position& heroPos,
                                                     const Position& anchor,
                                                     double desiredRange,
                                                     bool clockwise) const {
        const Position radial = normalizedOr(subtract(heroPos, anchor), Position(1.0, 0.0));
        const Position tangent = tangentFromRadial(radial, clockwise);
        const double currentDistance = heroPos.distanceTo(anchor);
        const double closenessRatio = std::clamp(
            (desiredRange - currentDistance) / std::max(desiredRange, 100.0),
            0.0,
            0.35
        );
        const double radialWeight = 0.82 + closenessRatio * 0.65;
        const double tangentialWeight = 0.48 - closenessRatio * 0.18;
        const Position escapeDirection = normalizedOr(
            add(scale(radial, radialWeight), scale(tangent, tangentialWeight)),
            radial
        );
        const double escapeRadius = desiredRange + std::max(55.0, desiredRange * 0.16);
        return add(anchor, scale(escapeDirection, escapeRadius));
    }

    [[nodiscard]] double idealCombatRange() const {
        const double attackRange = static_cast<double>(config_.attackRange);
        const double followDistance = static_cast<double>(config_.followDistance);
        return std::clamp(followDistance, 420.0, std::max(460.0, attackRange - 40.0));
    }

    [[nodiscard]] static double combinedDurabilityPercent(const ShipInfo& target) {
        const int32_t maxDurability =
            std::max(target.maxHealth, 0) + std::max(target.maxShield, 0);
        if (maxDurability <= 0) {
            return 100.0;
        }

        const int32_t currentDurability =
            std::max(target.health, 0) + std::max(target.shield, 0);
        return static_cast<double>(currentDurability) * 100.0 /
               static_cast<double>(maxDurability);
    }

    [[nodiscard]] bool allowsOwnedTarget(const NpcTargetConfig& config) const {
        return config_.targetEngagedNpc || config.ignoreOwnership;
    }

    [[nodiscard]] bool isLowHpFollowActive(const ShipInfo& target,
                                           const NpcTargetConfig& config) const {
        return config.followOnLowHp &&
               combinedDurabilityPercent(target) <=
                   static_cast<double>(std::clamp(config.followOnLowHpPercent, 1, 100));
    }

    [[nodiscard]] double configuredCombatRange(const NpcTargetConfig& config) const {
        return std::clamp(static_cast<double>(config.range), 200.0, 800.0);
    }

    [[nodiscard]] bool isCombatCollectEnabled(const GameSnapshot& snap) const {
        return collectConfig_.enabled &&
               collectConfig_.collectDuringCombat &&
               snap.mode == BotMode::KillCollect;
    }

    [[nodiscard]] bool shouldCollectCombatBox(const BoxInfo& box, const GameSnapshot& snap) const {
        for (const auto& targetBox : collectConfig_.targetBoxes) {
            if (!targetBox.enabled || targetBox.type != box.type) {
                continue;
            }

            if (targetBox.type == static_cast<int32_t>(BoxType::GreenBox) &&
                collectConfig_.skipBootyIfNoKeys &&
                snap.hero.bootyKeys <= 0) {
                return false;
            }

            if (targetBox.type == static_cast<int32_t>(BoxType::CargoBox) &&
                collectConfig_.skipResourceIfCargoFull &&
                snap.hero.isCargoFull()) {
                return false;
            }

            return true;
        }
        return false;
    }

    [[nodiscard]] int32_t combatCollectBoxPriority(int32_t boxType) const {
        for (const auto& targetBox : collectConfig_.targetBoxes) {
            if (targetBox.enabled && targetBox.type == boxType) {
                return targetBox.priority;
            }
        }
        return 0;
    }

    [[nodiscard]] int32_t highestCombatCollectPriority() const {
        int32_t best = 0;
        for (const auto& targetBox : collectConfig_.targetBoxes) {
            if (targetBox.enabled) {
                best = std::max(best, targetBox.priority);
            }
        }
        return best;
    }

    [[nodiscard]] double targetDurabilityRatio(const ShipInfo& target) const {
        const int64_t maxDurability =
            static_cast<int64_t>(std::max(target.maxHealth, 0)) +
            static_cast<int64_t>(std::max(target.maxShield, 0));
        const int64_t currentDurability =
            static_cast<int64_t>(std::max(target.health, 0)) +
            static_cast<int64_t>(std::max(target.shield, 0));

        if (maxDurability <= 0) {
            return 1.0;
        }

        return std::clamp(
            static_cast<double>(currentDurability) / static_cast<double>(maxDurability),
            0.0,
            1.0
        );
    }

    [[nodiscard]] double dynamicCombatCollectDistance(const ShipInfo& target) const {
        const double maxDistance = std::max(
            static_cast<double>(collectConfig_.combatCollectMaxDistance),
            0.0
        );
        if (maxDistance <= 0.0) {
            return 0.0;
        }

        const double minDistance = std::min(250.0, maxDistance);
        return minDistance + (maxDistance - minDistance) * targetDurabilityRatio(target);
    }

    [[nodiscard]] std::optional<CombatCollectSelection>
    findCombatCollectBox(const GameSnapshot& snap,
                         const ShipInfo& target,
                         double combatRange) const {
        if (!isCombatCollectEnabled(snap)) {
            return std::nullopt;
        }

        const int32_t highestPriority = highestCombatCollectPriority();
        if (highestPriority <= 0) {
            return std::nullopt;
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position targetPos(target.x, target.y);
        const double heroToTargetDistance = heroPos.distanceTo(targetPos);
        const double dynamicMaxDistance = dynamicCombatCollectDistance(target);
        if (dynamicMaxDistance <= 0.0) {
            return std::nullopt;
        }

        const BoxInfo* bestBox = nullptr;
        double bestScore = -std::numeric_limits<double>::infinity();

        for (const auto& box : snap.entities.boxes) {
            if (!box.existsOnMap || !shouldCollectCombatBox(box, snap)) {
                continue;
            }

            const int32_t priority = combatCollectBoxPriority(box.type);
            if (priority < highestPriority) {
                continue;
            }

            const Position approachPos = collectApproachPosition(box);
            const double heroToBoxDistance = heroPos.distanceTo(approachPos);
            if (heroToBoxDistance > dynamicMaxDistance) {
                continue;
            }

            if (heroToTargetDistance > 0.0 && heroToBoxDistance >= heroToTargetDistance + 80.0) {
                continue;
            }

            const double targetToBoxDistance = targetPos.distanceTo(approachPos);
            const double rangePenalty = std::abs(targetToBoxDistance - combatRange);
            const double score =
                priority * 1400.0 -
                heroToBoxDistance * 1.15 -
                rangePenalty * 0.85;

            if (score > bestScore) {
                bestScore = score;
                bestBox = &box;
            }
        }

        if (!bestBox) {
            return std::nullopt;
        }

        CombatCollectSelection selection;
        selection.box = bestBox;
        selection.approachPosition = collectApproachPosition(*bestBox);
        return selection;
    }

    void tryCollectCombatBox(const GameSnapshot& snap,
                             const CombatCollectSelection& selection) {
        if (!selection.box || !selection.box->existsOnMap) {
            return;
        }

        if (snap.timestampMs - lastCombatCollectTime_ < collectConfig_.collectCooldownMs) {
            return;
        }
        if (snap.timestampMs < combatCollectBlockedUntilMs_) {
            return;
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position boxPos(selection.box->x, selection.box->y);
        const double actualDistance = heroPos.distanceTo(boxPos);
        const double approachDistance = heroPos.distanceTo(selection.approachPosition);
        if (std::min(actualDistance, approachDistance) > COMBAT_COLLECT_RANGE) {
            return;
        }

        lastCombatCollectTime_ = snap.timestampMs;
        combatCollectBlockedUntilMs_ =
            snap.timestampMs + collectPostAttemptWaitMs(*selection.box);
        engine_->collect(selection.box->id);
    }

    [[nodiscard]] double adaptiveCombatRange(const ShipInfo* target,
                                             const GameSnapshot&) const {
        if (target) {
            if (const auto* config = matchTargetConfig(target->name)) {
                const double configuredRange = configuredCombatRange(*config);
                return isLowHpFollowActive(*target, *config)
                    ? configuredRange * LOW_HP_FOLLOW_RANGE_SCALE
                    : configuredRange;
            }
        }

        return idealCombatRange();
    }

    void adoptOrbitDirection(const OrbitSolution& solution, int64_t now) {
        if (!solution.valid || solution.clockwise == orbitClockwise_) {
            return;
        }

        orbitClockwise_ = solution.clockwise;
        lastOrbitDirectionChangeAtMs_ = now;
    }

    void publishTelemetry(const GameSnapshot& snap,
                          const ShipInfo* target,
                          std::string_view decision) {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = state_;
        telemetry_.movementMode = currentMovementMode_;
        telemetry_.movementDecision = std::string(decision);
        telemetry_.targetId = target ? target->id : 0;
        telemetry_.targetName = target ? target->name : std::string{};
        telemetry_.targetDistance = target
            ? static_cast<float>(Position(snap.hero.x, snap.hero.y).distanceTo(Position(target->x, target->y)))
            : 0.0f;
        telemetry_.stuckRecoveryActive = recoveryUntilMs_ > snap.timestampMs;
        telemetry_.lastProgressMs = lastProgressTime_;
        telemetry_.lastRecoveryMs = lastRecoveryAtMs_;
    }

    void resetTracking(const GameSnapshot& snap, const ShipInfo* target) {
        trackedTargetId_ = target ? target->id : 0;
        lastTrackedHeroPos_ = Position(snap.hero.x, snap.hero.y);
        lastTrackedTargetDistance_ = target
            ? lastTrackedHeroPos_.distanceTo(Position(target->x, target->y))
            : std::numeric_limits<double>::max();
        currentTargetAcquiredAtMs_ = snap.timestampMs;
        lastProgressTime_ = snap.timestampMs;
        lastDamageProgressAtMs_ = snap.timestampMs;
        currentTargetDurability_ = target
            ? std::max(target->health, 0) + std::max(target->shield, 0)
            : 0;
        currentTargetRecoveries_ = 0;
        recoveryUntilMs_ = 0;

        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.targetId = target ? target->id : 0;
        telemetry_.targetName = target ? target->name : std::string{};
        telemetry_.lastProgressMs = lastProgressTime_;
    }

    void updateProgressSample(const GameSnapshot& snap, double targetDistance) {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const double heroDelta = heroPos.distanceTo(lastTrackedHeroPos_);
        const double distanceImprovement = lastTrackedTargetDistance_ - targetDistance;
        const double minHeroDelta = std::clamp(
            static_cast<double>(std::max(snap.hero.speed, 250)) * 0.12,
            30.0,
            80.0
        );

        if (heroDelta >= minHeroDelta || distanceImprovement >= 45.0) {
            lastProgressTime_ = snap.timestampMs;
        }

        lastTrackedHeroPos_ = heroPos;
        lastTrackedTargetDistance_ = targetDistance;
    }

    void triggerStuckRecovery(const GameSnapshot& snap,
                              const ShipInfo* target,
                              std::string_view reason) {
        if (!target) {
            return;
        }

        if (movement_) {
            movement_->release(name());
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const MapBounds bounds = currentBounds();
        const double recoveryRange = std::max(160.0, adaptiveCombatRange(target, snap));
        const double heroSpeed = static_cast<double>(std::max(snap.hero.speed, 250));
        const double npcSpeed = target->observedSpeedEma > 10.0f
            ? std::clamp(static_cast<double>(target->observedSpeedEma), 90.0, 520.0)
            : (target->speed > 0
                ? std::clamp(static_cast<double>(target->speed), 90.0, 520.0)
                : 0.0);
        const OrbitSolution recoveryOrbit = selectOrbitSolution(
            snap,
            *target,
            recoveryRange,
            heroSpeed,
            npcSpeed,
            !orbitClockwise_
        );
        const Position recoveryFallback = navigation_.orbitPosition(
            heroPos,
            Position(target->x, target->y),
            recoveryRange,
            !orbitClockwise_
        );
        recoveryMoveTarget_ = recoveryOrbit.valid ? recoveryOrbit.point : recoveryFallback;
        recoveryMoveTarget_ = bounds.clamp(recoveryMoveTarget_, 220.0);

        moveWithinBounds(snap, recoveryMoveTarget_, MoveIntent::Escape);

        orbitClockwise_ = recoveryOrbit.valid ? recoveryOrbit.clockwise : !orbitClockwise_;
        lastOrbitDirectionChangeAtMs_ = snap.timestampMs;
        ++currentTargetRecoveries_;
        recoveryUntilMs_ = snap.timestampMs + 900;
        lastRecoveryAtMs_ = snap.timestampMs;
        lastProgressTime_ = snap.timestampMs;
        lastTrackedHeroPos_ = heroPos;
        lastTrackedTargetDistance_ = heroPos.distanceTo(Position(target->x, target->y));

        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_.stuckRecoveries++;
            telemetry_.recoveryReason = std::string(reason);
            telemetry_.lastRecoveryMs = lastRecoveryAtMs_;
            telemetry_.stuckRecoveryActive = true;
        }

        std::cout << "[Combat] Stuck recovery: " << reason
                  << " -> (" << recoveryMoveTarget_.x << ", " << recoveryMoveTarget_.y << ")\n";
    }

    [[nodiscard]] bool maybeHandleStuckRecovery(const GameSnapshot& snap,
                                                const ShipInfo* target,
                                                double targetDistance,
                                                int64_t timeoutMs,
                                                std::string_view reason) {
        if (!target) {
            return false;
        }

        if (trackedTargetId_ != target->id) {
            resetTracking(snap, target);
            return false;
        }

        if (recoveryUntilMs_ > snap.timestampMs) {
            publishTelemetry(snap, target, "Recovery");
            return true;
        }

        updateProgressSample(snap, targetDistance);
        if (snap.timestampMs - lastProgressTime_ < timeoutMs) {
            return false;
        }

        triggerStuckRecovery(snap, target, reason);
        publishTelemetry(snap, target, "Recovery");
        return true;
    }
    void observeTargetDurability(const GameSnapshot& snap, const ShipInfo& target) {
        const int32_t durability = std::max(target.health, 0) + std::max(target.shield, 0);
        if (currentTargetDurability_ == 0 || durability < currentTargetDurability_) {
            lastDamageProgressAtMs_ = snap.timestampMs;
        }
        currentTargetDurability_ = durability;
    }

    [[nodiscard]] bool isCurrentTargetValid(const GameSnapshot& snap,
                                            const ShipInfo& target,
                                            const NpcTargetConfig& config) const {
        if (target.isDestroyed) {
            return false;
        }
        if (isTargetLockedOut(target.id, snap.timestampMs)) {
            return false;
        }
        if (!allowsOwnedTarget(config) && isNpcEngagedByOtherShip(snap, target)) {
            return false;
        }

        const double maxDistance = effectiveMaxDistance(config);
        if (maxDistance <= 0.0 || isLowHpFollowActive(target, config)) {
            return true;
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const double dist = heroPos.distanceTo(Position(target.x, target.y));
        return dist <= maxDistance + TARGET_LEASH_DISTANCE;
    }

    [[nodiscard]] bool isNpcEngagedByOtherShip(const GameSnapshot& snap,
                                               const ShipInfo& npc) const {
        for (const auto& ship : snap.entities.ships) {
            if (ship.id == 0 || ship.id == snap.hero.id || ship.id == npc.id) {
                continue;
            }
            if (ship.isDestroyed || ship.isCloaked || ship.isNpc) {
                continue;
            }
            if (ship.selectedTarget == npc.id && ship.isAttacking) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<CombatTargetSelection>
    findBestTarget(const GameSnapshot& snap) {
        std::optional<CombatTargetSelection> best;
        const Position heroPos(snap.hero.x, snap.hero.y);

        for (const auto& ship : snap.entities.ships) {
            if (ship.isDestroyed || ship.isCloaked || ship.id == snap.hero.id) {
                continue;
            }

            const NpcTargetConfig* matchedConfig = matchTargetConfig(ship.name);
            if (!matchedConfig || !shouldTreatAsConfiguredNpc(ship, snap)) {
                continue;
            }
            if (!allowsOwnedTarget(*matchedConfig) &&
                isNpcEngagedByOtherShip(snap, ship)) {
                continue;
            }

            const double dist = heroPos.distanceTo(Position(ship.x, ship.y));
            const double maxDistance = effectiveMaxDistance(*matchedConfig);
            if (maxDistance > 0.0 && dist > maxDistance) {
                continue;
            }

            if (isTargetLockedOut(ship.id, snap.timestampMs)) {
                continue;
            }

            const double durabilityRatio = targetDurabilityRatio(ship);
            double score = matchedConfig->priority * 10000.0 - dist;
            if (snap.hero.selectedTarget == ship.id) {
                score += 1100.0;
            }
            if (ship.selectedTarget == snap.hero.id && ship.isAttacking) {
                score += 2600.0;
            }
            score += (1.0 - durabilityRatio) * 650.0;

            const CombatTargetSelection candidate{&ship, matchedConfig, dist, durabilityRatio, score};
            if (!best.has_value() ||
                candidate.score > best->score + 1.0 ||
                (std::abs(candidate.score - best->score) <= 1.0 &&
                 (candidate.distance + 25.0 < best->distance ||
                  (std::abs(candidate.distance - best->distance) <= 25.0 &&
                   (candidate.durabilityRatio + 0.03 < best->durabilityRatio ||
                    (std::abs(candidate.durabilityRatio - best->durabilityRatio) <= 0.03 &&
                     candidate.ship->id < best->ship->id)))))) {
                best = candidate;
            }
        }

        return best;
    }
    
    [[nodiscard]] const NpcTargetConfig* matchTargetConfig(const std::string& name) const {
        for (std::size_t i = 0; i < config_.targets.size() && i < targetPatterns_.size(); ++i) {
            if (equalsIgnoreCase(name, config_.targets[i].namePattern)) {
                return &config_.targets[i];
            }
            if (std::regex_search(name, targetPatterns_[i])) {
                return &config_.targets[i];
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool shouldTreatAsConfiguredNpc(const ShipInfo& ship,
                                                  const GameSnapshot& snap) const {
        if (ship.id == 0 || ship.id == snap.hero.id || ship.name.empty()) {
            return false;
        }

        if (ship.isNpc) {
            return true;
        }

        if (!isKnownNpc(ship.name)) {
            return false;
        }

        if (snap.mapName.empty()) {
            return true;
        }

        return npcSpawnsOnMap(ship.name, snap.mapName);
    }
    
    [[nodiscard]] const ShipInfo* getCurrentTarget(const GameSnapshot& snap) const {
        if (currentTargetId_ == 0) {
            return nullptr;
        }

        for (const auto& ship : snap.entities.ships) {
            if (ship.id == currentTargetId_ && shouldTreatAsConfiguredNpc(ship, snap)) {
                return &ship;
            }
        }
        return nullptr;
    }

    void beginTarget(const GameSnapshot& snap, const CombatTargetSelection& selection) {
        currentTargetId_ = selection.ship->id;
        currentAmmoType_ = selection.config->ammoType;
        currentRocketType_ = selection.config->rocketType;
        currentMovementMode_ = CombatMovementMode::Adaptive;
        mixedUseKite_ = false;
        mixedMovementSwitchAtMs_ = 0;

        resetTracking(snap, selection.ship);
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_.movementMode = currentMovementMode_;
            telemetry_.lastModeChangeMs = snap.timestampMs;
            telemetry_.recoveryReason.clear();
        }

        std::cout << "[Combat] Found target: " << selection.ship->name
                  << " (id=" << currentTargetId_ << ", movement="
                  << movementModeName(currentMovementMode_) << ")\n";

        engine_->lockTarget(currentTargetId_);
        lastSelectTime_ = snap.timestampMs;
        state_ = CombatState::Approaching;
        publishTelemetry(snap, selection.ship, "TargetAcquired");
    }
    
    void handleSearching(const GameSnapshot& snap) {
        auto selection = findBestTarget(snap);
        if (!selection.has_value()) {
            publishTelemetry(snap, nullptr, "Searching");
            return;
        }

        beginTarget(snap, *selection);
    }
    
    void handleApproaching(const GameSnapshot& snap) {
        const ShipInfo* target = getCurrentTarget(snap);
        if (!target) {
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "TargetLost");
            return;
        }

        const NpcTargetConfig* targetConfig = matchTargetConfig(target->name);
        if (!targetConfig || !isCurrentTargetValid(snap, *target, *targetConfig)) {
            applyTargetLockout(currentTargetId_, snap.timestampMs, "invalid_target", 7000);
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "InvalidTarget");
            return;
        }

        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position targetPos(target->x, target->y);
        const double dist = heroPos.distanceTo(targetPos);

        if (now - lastSelectTime_ >= config_.selectCooldownMs) {
            lastSelectTime_ = now;
            syncWeaponLoadout(snap, *target, now);
        }

        if (maybeHandleStuckRecovery(snap, target, dist, 2200, "approach_stalled")) {
            if (currentTargetRecoveries_ > MAX_TARGET_RECOVERIES) {
                applyTargetLockout(currentTargetId_, now, "approach_stalled");
                stopCombatActions(true, true);
                clearCurrentTargetState();
                state_ = CombatState::Searching;
                publishTelemetry(snap, nullptr, "ApproachLockout");
            }
            return;
        }

        if (currentTargetAcquiredAtMs_ > 0 &&
            now - currentTargetAcquiredAtMs_ > APPROACH_TIMEOUT_MS) {
            applyTargetLockout(currentTargetId_, now, "approach_timeout");
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "ApproachTimeout");
            return;
        }

        if (dist <= config_.attackRange && snap.hero.selectedTarget == currentTargetId_) {
            state_ = CombatState::Attacking;
            lastAttackTime_ = 0;
            std::cout << "[Combat] In range, engaging " << target->name << "\n";
            publishTelemetry(snap, target, "ReadyToAttack");
            return;
        }

        if (now - lastMoveTime_ >= config_.moveCooldownMs) {
            lastMoveTime_ = now;

            const double heroSpd = static_cast<double>(std::max(snap.hero.speed, 250));
            const double npcSpd = target->observedSpeedEma > 10.0f
                ? std::clamp(static_cast<double>(target->observedSpeedEma), 90.0, 520.0)
                : (target->speed > 0
                    ? std::clamp(static_cast<double>(target->speed), 90.0, 520.0)
                    : 0.0);
            const double approachRange = std::min(
                adaptiveCombatRange(target, snap),
                std::max(160.0, static_cast<double>(config_.attackRange) - ATTACK_READY_MARGIN)
            );
            const OrbitSolution approachSolution = selectApproachEntrySolution(
                snap,
                *target,
                approachRange,
                heroSpd,
                npcSpd,
                orbitClockwise_
            );
            adoptOrbitDirection(approachSolution, now);

            moveWithinBounds(snap, approachSolution.point, MoveIntent::Pursuit);
        }
        publishTelemetry(
            snap,
            target,
            snap.hero.selectedTarget == currentTargetId_ ? "ClosingForAttack" : "LockingTarget"
        );
    }
    
    void handleAttacking(const GameSnapshot& snap) {
        const ShipInfo* target = getCurrentTarget(snap);
        if (!target) {
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Cooldown;
            publishTelemetry(snap, nullptr, "TargetGone");
            return;
        }

        const NpcTargetConfig* targetConfig = matchTargetConfig(target->name);
        if (!targetConfig || !isCurrentTargetValid(snap, *target, *targetConfig)) {
            applyTargetLockout(currentTargetId_, snap.timestampMs, "invalid_target", 7000);
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "InvalidTarget");
            return;
        }

        const auto now = snap.timestampMs;
        if (now - lastSelectTime_ >= config_.selectCooldownMs) {
            lastSelectTime_ = now;
            syncWeaponLoadout(snap, *target, now);
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const double combatRange = adaptiveCombatRange(target, snap);
        const double repositionThreshold =
            std::max(static_cast<double>(config_.attackRange), combatRange) + rangeSlack(combatRange);
        const double dist = heroPos.distanceTo(Position(target->x, target->y));
        if (dist > repositionThreshold) {
            state_ = CombatState::Approaching;
            publishTelemetry(snap, target, "Reposition");
            return;
        }

        if (snap.hero.selectedTarget != currentTargetId_) {
            state_ = CombatState::Approaching;
            publishTelemetry(snap, target, "Relock");
            return;
        }

        if (now - lastAttackTime_ >= config_.attackCooldownMs) {
            lastAttackTime_ = now;

            engine_->attack();
            killStartTime_ = now;
            std::cout << "[Combat] Attacking " << target->name << "\n";
        }

        handleCombatMovement(snap, target);
        state_ = CombatState::AwaitingKill;
        publishTelemetry(snap, target, "AttackStart");
    }
    
    void handleAwaitingKill(const GameSnapshot& snap) {
        const ShipInfo* target = getCurrentTarget(snap);
        if (!target) {
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Cooldown;
            std::cout << "[Combat] Target eliminated\n";
            publishTelemetry(snap, nullptr, "TargetEliminated");
            return;
        }

        const NpcTargetConfig* targetConfig = matchTargetConfig(target->name);
        if (!targetConfig || !isCurrentTargetValid(snap, *target, *targetConfig)) {
            applyTargetLockout(currentTargetId_, snap.timestampMs, "invalid_target", 7000);
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "InvalidTarget");
            return;
        }

        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);
        const double combatRange = adaptiveCombatRange(target, snap);
        const double sustainedAttackEnvelope =
            std::max(static_cast<double>(config_.attackRange), combatRange) + rangeSlack(combatRange);
        const double dist = heroPos.distanceTo(Position(target->x, target->y));

        observeTargetDurability(snap, *target);

        if (now - lastSelectTime_ >= config_.selectCooldownMs) {
            lastSelectTime_ = now;
            syncWeaponLoadout(snap, *target, now);
        }

        if (snap.hero.selectedTarget != currentTargetId_) {
            state_ = CombatState::Approaching;
            publishTelemetry(snap, target, "Relock");
            return;
        }

        if (!snap.hero.isAttacking) {
            state_ = CombatState::Attacking;
            publishTelemetry(snap, target, "Reattack");
            return;
        }

        if (dist <= sustainedAttackEnvelope &&
            now - lastDamageProgressAtMs_ > DAMAGE_STALL_TIMEOUT_MS) {
            applyTargetLockout(currentTargetId_, now, "damage_stall");
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "DamageStall");
            return;
        }

        handleCombatMovement(snap, target);
    }
    
    void handleCooldown(const GameSnapshot& snap) {
        state_ = CombatState::Searching;
        clearCurrentTargetState();
        publishTelemetry(snap, nullptr, "Cooldown");
    }
    
    void handleCombatMovement(const GameSnapshot& snap, const ShipInfo* target) {
        auto now = snap.timestampMs;

        if (now - lastMoveTime_ < config_.moveCooldownMs) {
            return;
        }

        const double combatRange = adaptiveCombatRange(target, snap);
        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position targetPos(target->x, target->y);
        const double currentDistance = heroPos.distanceTo(targetPos);
        const double heroSpeed = static_cast<double>(std::max(snap.hero.speed, 250));
        const double npcSpeed = target->observedSpeedEma > 10.0f
            ? std::clamp(static_cast<double>(target->observedSpeedEma), 90.0, 520.0)
            : (target->speed > 0
                ? std::clamp(static_cast<double>(target->speed), 90.0, 520.0)
                : 0.0);
        const CombatTargetMotion motion = buildTargetMotion(heroPos, *target, heroSpeed, combatRange);
        const double anchorDistance = heroPos.distanceTo(motion.anchor);
        const auto combatCollect = findCombatCollectBox(snap, *target, combatRange);

        if (maybeHandleStuckRecovery(snap, target, currentDistance, 2600, "combat_movement_stalled")) {
            return;
        }

        const double radialSlack = rangeSlack(combatRange);
        if (anchorDistance > combatRange + radialSlack) {
            lastMoveTime_ = now;
            const Position moveTarget = navigation_.approachPosition(heroPos, motion.anchor, combatRange);
            moveWithinBounds(snap, moveTarget, MoveIntent::Pursuit);
            if (combatCollect.has_value()) {
                tryCollectCombatBox(snap, *combatCollect);
            }
            publishTelemetry(snap, target, "Pursuit");
            return;
        }

        if (anchorDistance < combatRange - radialSlack) {
            lastMoveTime_ = now;
            const Position createSpace = selectCreateSpacePosition(
                heroPos,
                motion.anchor,
                combatRange,
                orbitClockwise_
            );
            moveWithinBounds(snap, createSpace, MoveIntent::Escape);
            if (combatCollect.has_value()) {
                tryCollectCombatBox(snap, *combatCollect);
            }
            publishTelemetry(snap, target, "CreateSpace");
            return;
        }

        lastMoveTime_ = now;
        const OrbitSolution orbitSolution = selectOrbitSolution(
            snap,
            *target,
            combatRange,
            heroSpeed,
            npcSpeed,
            orbitClockwise_,
            combatCollect.has_value() ? combatCollect->box : nullptr
        );
        adoptOrbitDirection(orbitSolution, now);

        const Position orbitPos = orbitSolution.valid
            ? orbitSolution.point
            : navigation_.approachPosition(heroPos, motion.anchor, combatRange);
        moveWithinBounds(snap, orbitPos, MoveIntent::Orbit);
        if (combatCollect.has_value()) {
            tryCollectCombatBox(snap, *combatCollect);
        }

        if (combatCollect.has_value() && combatCollect->box) {
            publishTelemetry(snap, target, "Orbit+Collect");
        } else {
            publishTelemetry(snap, target, "Orbit");
        }
    }
    
    void handleAntibanMovement(const GameSnapshot& snap) {
        auto now = snap.timestampMs;
        
        if (now - lastAntibanMoveTime_ >= config_.antibanMoveIntervalMs) {
            lastAntibanMoveTime_ = now;
            
            Position heroPos(snap.hero.x, snap.hero.y);
            Position jittered = navigation_.jitter(heroPos, 100);
            
            const MapBounds bounds = currentBounds();
            if (bounds.contains(jittered, 200)) {
                moveWithinBounds(snap, jittered, MoveIntent::Antiban);
            }
            publishTelemetry(snap, getCurrentTarget(snap), "AntibanJitter");
        }
    }

    void syncWeaponLoadout(const GameSnapshot& snap, const ShipInfo& target, int64_t now) {
        engine_->switchAmmo(currentAmmoType_);

        const Position heroPos(snap.hero.x, snap.hero.y);
        const double targetDistance = heroPos.distanceTo(Position(target.x, target.y));
        if (snap.hero.selectedTarget != currentTargetId_ &&
            targetDistance <= targetSelectionRange()) {
            engine_->lockTarget(currentTargetId_);
        }

        const bool wantsAutoRocket = config_.useRockets && currentRocketType_ > 0;
        if (!wantsAutoRocket) {
            disableAutoRocket(now);
            return;
        }

        if (now - lastRocketSwitchTime_ >= config_.rocketCooldownMs) {
            engine_->switchRocket(currentRocketType_);
            lastRocketSwitchTime_ = now;
        }

        enableAutoRocket(now);
    }

    void enableAutoRocket(int64_t now) {
        if (autoRocketStateKnown_ && autoRocketEnabled_) {
            return;
        }
        if (now > 0 && now - lastAutoRocketToggleTime_ < AUTO_ROCKET_TOGGLE_COOLDOWN_MS) {
            return;
        }

        engine_->setAutoRocketEnabled(true);
        autoRocketEnabled_ = true;
        autoRocketStateKnown_ = true;
        lastAutoRocketToggleTime_ = now;
    }

    void disableAutoRocket(int64_t now, bool force = false) {
        if (!force && autoRocketStateKnown_ && !autoRocketEnabled_) {
            return;
        }
        if (!force && now > 0 &&
            now - lastAutoRocketToggleTime_ < AUTO_ROCKET_TOGGLE_COOLDOWN_MS) {
            return;
        }

        engine_->setAutoRocketEnabled(false);
        autoRocketEnabled_ = false;
        autoRocketStateKnown_ = true;
        lastAutoRocketToggleTime_ = now;
    }
};

} // namespace dynamo
