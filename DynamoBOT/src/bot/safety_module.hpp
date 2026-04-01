#pragma once

/**
 * @file safety_module.hpp
 * @brief Safety module - handles emergency flee, repair, enemy detection
 *
 * Highest priority module. Takes over when danger is detected.
 */

#include "module.hpp"
#include "bot_config.hpp"
#include "threat_tracker.hpp"
#include "navigation.hpp"
#include "movement_controller.hpp"
#include "../game/game_engine.hpp"

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

/**
 * @brief Safety module states
 */
enum class SafetyState {
    Monitoring,         // Normal state, checking for threats
    Fleeing,            // Running away from danger
    AtPortal,           // Reached escape portal
    AwaitingTeleport,   // Teleport sent, waiting for map change
    Repairing,          // Low HP, waiting to repair
    ConfigSwitching     // Switching ship config
};

enum class RepairAnchorKind {
    None,
    Portal,
    Station
};

struct RepairAnchor {
    RepairAnchorKind kind{RepairAnchorKind::None};
    int32_t id{0};
    Position position;
    std::string label;
};

struct SafetySessionState {
    std::unordered_set<int32_t> rememberedAggressorIds;

    void reset() {
        rememberedAggressorIds.clear();
    }
};

struct SafetyTelemetry {
    SafetyState state{SafetyState::Monitoring};
    ThreatLevel threatLevel{ThreatLevel::None};
    std::string decision{"Monitoring"};
    std::string escapeMap;
    int32_t escapePortalId{0};
    int32_t visibleEnemies{0};
    int32_t closeEnemies{0};
    int32_t attackers{0};
    int32_t primaryThreatId{0};
    float hpPercent{100.0f};
    float primaryThreatDistance{0.0f};
    float fleeTargetX{0.0f};
    float fleeTargetY{0.0f};
    bool beingAttacked{false};
    bool adminSeenRecently{false};
    bool hostileApproachDetected{false};
    int32_t fleeRetargets{0};
    int64_t lastStateChangeMs{0};
    int64_t lastProgressMs{0};
};

[[nodiscard]] inline const char* repairAnchorKindName(RepairAnchorKind kind) {
    switch (kind) {
        case RepairAnchorKind::Portal: return "portal";
        case RepairAnchorKind::Station: return "station";
        case RepairAnchorKind::None:
        default: return "anchor";
    }
}

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
    SafetyModule(std::shared_ptr<GameEngine> engine,
                 std::shared_ptr<MovementController> movement,
                 const SafetyConfig& config,
                 const MapConfig& mapConfig,
                 const AdminConfig& adminConfig,
                 std::shared_ptr<SafetySessionState> sessionState = std::make_shared<SafetySessionState>())
        : Module(std::move(engine), std::move(movement))
        , config_(config)
        , mapConfig_(mapConfig)
        , adminConfig_(adminConfig)
        , sessionState_(sessionState ? std::move(sessionState) : std::make_shared<SafetySessionState>())
        , threatTracker_(adminConfig.droneCountThreshold) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Safety";
    }

    [[nodiscard]] int getPriority(const GameSnapshot& snap) override {
        if (!config_.enabled || !enabled_) {
            return 0;
        }

        updateThreats(snap);

        const double hpPercent = calculateHpPercent(snap);
        const auto summary = threatTracker_.summarize(
            hpPercent,
            snap.hero.x,
            snap.hero.y,
            snap.timestampMs,
            config_.enemySeenTimeoutMs
        );
        const auto assessment = assessEnemyFlee(snap, summary);

        if (summary.adminNearby || hpPercent < config_.minHpPercent) {
            return config_.priority + 10;
        }

        if (assessment.active) {
            return summary.beingAttacked ? config_.priority : config_.priority - 5;
        }

        if (state_ == SafetyState::Fleeing || state_ == SafetyState::AtPortal) {
            return config_.priority - 5;
        }

        // Must hold maximum priority while waiting for teleport to complete
        if (state_ == SafetyState::AwaitingTeleport) {
            return config_.priority + 10;
        }

        if (state_ == SafetyState::Repairing) {
            // During revive grace period, boost priority so Travel can't override
            if (reviveGraceUntilMs_ > 0 && snap.timestampMs < reviveGraceUntilMs_) {
                return config_.priority + 5;
            }
            return config_.priority - 10;
        }

        if (hpPercent < config_.repairHpPercent) {
            return config_.priority - 10;
        }

        if (threatTracker_.adminSeenRecently(
                snap.timestampMs,
                std::max(config_.adminEscapeDelayMs, adminConfig_.escapeDelayMs))) {
            return config_.priority - 15;
        }

        return 0;
    }

    void onStart() override {
        transitionTo(SafetyState::Monitoring, 0);
        clearEscapeAnchor();
        lastFleeDistance_ = std::numeric_limits<double>::max();
        lastProgressTime_ = 0;
        fleeRetargetCount_ = 0;
        clearRepairAnchor();
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_ = SafetyTelemetry{};
        }
        std::cout << "[Safety] Module activated - assessing threats\n";
    }

    void onStop() override {
        if (movement_) {
            movement_->release(name());
        }
        transitionTo(SafetyState::Monitoring, 0);
        clearEscapeAnchor();
        lastFleeDistance_ = std::numeric_limits<double>::max();
        clearRepairAnchor();
        publishTelemetry(GameSnapshot{}, ThreatSummary{}, "Stopped");
        std::cout << "[Safety] Module deactivated\n";
    }

    void tick(const GameSnapshot& snap) override {
        const double hpPercent = calculateHpPercent(snap);
        const auto summary = threatTracker_.summarize(
            hpPercent,
            snap.hero.x,
            snap.hero.y,
            snap.timestampMs,
            config_.enemySeenTimeoutMs
        );

        switch (state_) {
            case SafetyState::Monitoring:
                handleMonitoring(snap, hpPercent, summary);
                break;

            case SafetyState::Fleeing:
                handleFleeing(snap, hpPercent, summary);
                break;

            case SafetyState::AtPortal:
                handleAtPortal(snap, hpPercent, summary);
                break;

            case SafetyState::AwaitingTeleport:
                handleAwaitingTeleport(snap, hpPercent, summary);
                break;

            case SafetyState::Repairing:
                handleRepairing(snap, hpPercent, summary);
                break;

            case SafetyState::ConfigSwitching:
                handleConfigSwitching(snap, summary);
                break;
        }
    }

    [[nodiscard]] SafetyState getState() const noexcept { return state_; }
    [[nodiscard]] const ThreatTracker& getThreatTracker() const noexcept { return threatTracker_; }
    [[nodiscard]] SafetyTelemetry getTelemetry() const {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        return telemetry_;
    }

    void resetAfterRevive(int64_t nowMs = 0) {
        if (movement_) {
            movement_->release(name());
        }

        threatTracker_.clear();
        // Go directly to Repairing — hero always needs repair after death.
        // Grace period prevents Travel from overriding us before server
        // confirms the safe zone flag.
        transitionTo(SafetyState::Repairing, nowMs);
        reviveGraceUntilMs_ = nowMs + REVIVE_GRACE_PERIOD_MS;
        repairHoldIssued_ = false;
        clearEscapeAnchor();
        fleeTarget_ = Position{};
        lastFleeDistance_ = std::numeric_limits<double>::max();
        lastMoveTime_ = 0;
        lastProgressTime_ = 0;
        fleeRetargetCount_ = 0;
        clearRepairAnchor();
        teleportFromMap_.clear();
        teleportSentAtMs_ = 0;

        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_ = SafetyTelemetry{};
        telemetry_.state = state_;
        telemetry_.lastStateChangeMs = nowMs;
    }

private:
    SafetyConfig config_;
    MapConfig mapConfig_;
    AdminConfig adminConfig_;
    std::shared_ptr<SafetySessionState> sessionState_;
    ThreatTracker threatTracker_;
    Navigation navigation_;
    SafetyState state_{SafetyState::Monitoring};

    int64_t lastMoveTime_{0};
    int64_t lastConfigSwitchTime_{0};
    int64_t lastProgressTime_{0};
    double lastFleeDistance_{std::numeric_limits<double>::max()};
    Position fleeTarget_;
    int32_t escapePortalId_{0};
    std::string escapePortalMap_;
    std::optional<RepairAnchor> escapeAnchor_;
    int32_t fleeRetargetCount_{0};
    std::optional<RepairAnchor> repairAnchor_;
    bool repairHoldIssued_{false};
    int64_t reviveGraceUntilMs_{0};
    std::string teleportFromMap_;      // Map name when teleport was sent
    int64_t teleportSentAtMs_{0};      // When teleport packet was sent
    bool unsafePortalLogged_{false};   // Avoid spamming "unsafe portal" log
    mutable std::mutex telemetryMutex_;
    SafetyTelemetry telemetry_;

    static constexpr int64_t FLEE_STALL_TIMEOUT_MS = 2800;
    static constexpr int64_t TELEPORT_TIMEOUT_MS = 8000;
    static constexpr int64_t REPAIR_STALL_TIMEOUT_MS = 4500;
    static constexpr int64_t REPAIR_HOLD_REISSUE_MS = 1200;
    static constexpr int64_t REVIVE_GRACE_PERIOD_MS = 5000;
    static constexpr double REPAIR_PROGRESS_DELTA = 60.0;
    static constexpr double REPAIR_PORTAL_ARRIVAL_RADIUS = 220.0;
    static constexpr double REPAIR_STATION_ARRIVAL_RADIUS = 260.0;
    static constexpr double ESCAPE_CORRIDOR_DANGER_RADIUS = 700.0;
    static constexpr double ESCAPE_ENEMY_PROXIMITY_RADIUS = 1100.0;

    struct EnemyFleeAssessment {
        bool active{false};
        bool visibleRememberedAggressor{false};
        bool adminOverride{false};
        std::optional<EnemyInfo> primaryThreat;
        std::vector<EnemyInfo> visibleEnemies;
    };

    void transitionTo(SafetyState newState, int64_t nowMs) {
        if (state_ == newState) {
            return;
        }
        state_ = newState;
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = newState;
        telemetry_.lastStateChangeMs = nowMs;
    }

    void updateThreats(const GameSnapshot& snap) {
        for (const auto& ship : snap.entities.enemies) {
            const bool attackingUs = ship.selectedTarget == snap.hero.id && ship.isAttacking;
            threatTracker_.updateEnemy(EnemyObservation{
                .playerId = ship.id,
                .username = ship.name,
                .x = ship.x,
                .y = ship.y,
                .targetX = ship.targetX,
                .targetY = ship.targetY,
                .isMoving = ship.isMoving,
                .droneCount = ship.droneCount,
                .isAttackingUs = attackingUs,
                .timestampMs = snap.timestampMs,
            });

            if (attackingUs && sessionState_) {
                sessionState_->rememberedAggressorIds.insert(ship.id);
            }
        }

        threatTracker_.cleanupOldEnemies(snap.timestampMs, config_.enemySeenTimeoutMs);
    }

    [[nodiscard]] double calculateHpPercent(const GameSnapshot& snap) const {
        if (snap.hero.maxHealth <= 0) return 100.0;
        return (static_cast<double>(snap.hero.health) / snap.hero.maxHealth) * 100.0;
    }

    [[nodiscard]] static double distanceToSegment(const Position& point,
                                                  const Position& segmentStart,
                                                  const Position& segmentEnd) {
        const double segmentDx = segmentEnd.x - segmentStart.x;
        const double segmentDy = segmentEnd.y - segmentStart.y;
        const double segmentLengthSq = segmentDx * segmentDx + segmentDy * segmentDy;
        if (segmentLengthSq <= 1e-6) {
            return point.distanceTo(segmentStart);
        }

        const double t = std::clamp(
            ((point.x - segmentStart.x) * segmentDx + (point.y - segmentStart.y) * segmentDy) /
                segmentLengthSq,
            0.0,
            1.0
        );
        const Position projection{
            segmentStart.x + segmentDx * t,
            segmentStart.y + segmentDy * t
        };
        return point.distanceTo(projection);
    }

    [[nodiscard]] static double normalizedDot(const Position& lhs, const Position& rhs) {
        const double lhsLength = std::sqrt(lhs.x * lhs.x + lhs.y * lhs.y);
        const double rhsLength = std::sqrt(rhs.x * rhs.x + rhs.y * rhs.y);
        if (lhsLength <= 1e-6 || rhsLength <= 1e-6) {
            return 0.0;
        }
        return ((lhs.x * rhs.x) + (lhs.y * rhs.y)) / (lhsLength * rhsLength);
    }

    [[nodiscard]] std::optional<EnemyInfo> findFreshEnemyById(int32_t playerId,
                                                              int64_t nowMs) const {
        return threatTracker_.getFreshEnemy(playerId, nowMs, config_.enemySeenTimeoutMs);
    }

    [[nodiscard]] std::optional<EnemyInfo> pickNearestEnemy(const std::vector<EnemyInfo>& enemies,
                                                            const Position& heroPos) const {
        if (enemies.empty()) {
            return std::nullopt;
        }

        const EnemyInfo* best = nullptr;
        double bestDistance = std::numeric_limits<double>::max();
        for (const auto& enemy : enemies) {
            const double distance = heroPos.distanceTo(Position(enemy.lastX, enemy.lastY));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = &enemy;
            }
        }

        return best ? std::optional<EnemyInfo>(*best) : std::nullopt;
    }

    [[nodiscard]] EnemyFleeAssessment assessEnemyFlee(const GameSnapshot& snap,
                                                      const ThreatSummary& summary) const {
        EnemyFleeAssessment assessment;
        assessment.visibleEnemies = threatTracker_.freshEnemies(
            snap.timestampMs,
            config_.enemySeenTimeoutMs
        );

        const Position heroPos(snap.hero.x, snap.hero.y);
        if (summary.primaryThreatId != 0) {
            assessment.primaryThreat = findFreshEnemyById(summary.primaryThreatId, snap.timestampMs);
        }

        const auto chooseRememberedAggressor = [&]() -> std::optional<EnemyInfo> {
            const auto rememberedAggressorIds = sessionState_
                ? sessionState_->rememberedAggressorIds
                : std::unordered_set<int32_t>{};
            const EnemyInfo* best = nullptr;
            double bestDistance = std::numeric_limits<double>::max();
            bool bestIsCurrentAttacker = false;

            for (const auto& enemy : assessment.visibleEnemies) {
                if (!rememberedAggressorIds.contains(enemy.playerId)) {
                    continue;
                }

                const bool currentAttacker = enemy.isAttackingUs;
                const double distance = heroPos.distanceTo(Position(enemy.lastX, enemy.lastY));
                if (best == nullptr ||
                    (currentAttacker && !bestIsCurrentAttacker) ||
                    (currentAttacker == bestIsCurrentAttacker && distance < bestDistance)) {
                    best = &enemy;
                    bestDistance = distance;
                    bestIsCurrentAttacker = currentAttacker;
                }
            }

            if (best != nullptr) {
                assessment.visibleRememberedAggressor = true;
                return *best;
            }
            return std::nullopt;
        };

        if (summary.adminNearby) {
            assessment.adminOverride = true;
            assessment.active = true;
            if (!assessment.primaryThreat.has_value()) {
                assessment.primaryThreat = pickNearestEnemy(assessment.visibleEnemies, heroPos);
            }
            return assessment;
        }

        switch (config_.fleeMode) {
            case SafetyFleeMode::OnEnemySeen:
                assessment.active = !assessment.visibleEnemies.empty();
                if (assessment.active && !assessment.primaryThreat.has_value()) {
                    assessment.primaryThreat = pickNearestEnemy(assessment.visibleEnemies, heroPos);
                }
                break;

            case SafetyFleeMode::OnAttack:
                if (summary.beingAttacked) {
                    assessment.active = true;
                    if (const auto* attacker = threatTracker_.getAttacker(snap.timestampMs);
                        attacker != nullptr) {
                        assessment.primaryThreat = *attacker;
                        break;
                    }
                }

                assessment.primaryThreat = chooseRememberedAggressor();
                assessment.active = assessment.primaryThreat.has_value();
                break;

            case SafetyFleeMode::None:
            default:
                break;
        }

        return assessment;
    }

    [[nodiscard]] double scoreEscapeAnchor(const Position& heroPos,
                                           const RepairAnchor& anchor,
                                           const EnemyFleeAssessment& assessment) const {
        double score = heroPos.distanceTo(anchor.position);

        if (anchor.kind == RepairAnchorKind::Station) {
            score -= 120.0;
        }

        if (anchor.kind == RepairAnchorKind::Portal) {
            if (!anchor.label.empty() && isUnsafePortalTarget(anchor.label)) {
                score += 5000.0;  // Heavily penalize unsafe destinations
            } else if (!anchor.label.empty() && isAvoidedMap(anchor.label)) {
                score += 3200.0;
            } else {
                score -= 180.0;
            }
        }

        if (!assessment.primaryThreat.has_value()) {
            return score;
        }

        const Position primaryThreatPos(
            assessment.primaryThreat->lastX,
            assessment.primaryThreat->lastY
        );
        const double heroThreatDistance = heroPos.distanceTo(primaryThreatPos);
        const double anchorThreatDistance = anchor.position.distanceTo(primaryThreatPos);
        score += std::max(0.0, heroThreatDistance + 180.0 - anchorThreatDistance) * 3.0;
        score -= std::max(0.0, anchorThreatDistance - heroThreatDistance) * 0.45;

        const Position awayVector(heroPos.x - primaryThreatPos.x, heroPos.y - primaryThreatPos.y);
        const Position travelVector(anchor.position.x - heroPos.x, anchor.position.y - heroPos.y);
        score -= normalizedDot(travelVector, awayVector) * 520.0;

        for (const auto& enemy : assessment.visibleEnemies) {
            const Position enemyPos(enemy.lastX, enemy.lastY);
            const double heroEnemyDistance = heroPos.distanceTo(enemyPos);
            const double anchorEnemyDistance = anchor.position.distanceTo(enemyPos);
            const double corridorDistance = distanceToSegment(enemyPos, heroPos, anchor.position);

            if (anchorEnemyDistance < heroEnemyDistance) {
                score += (heroEnemyDistance - anchorEnemyDistance) * 1.6;
            } else {
                score -= std::min(anchorEnemyDistance - heroEnemyDistance, 600.0) * 0.18;
            }

            if (anchorEnemyDistance < ESCAPE_ENEMY_PROXIMITY_RADIUS) {
                score += (ESCAPE_ENEMY_PROXIMITY_RADIUS - anchorEnemyDistance) * 2.4;
            }

            if (corridorDistance < ESCAPE_CORRIDOR_DANGER_RADIUS) {
                score += (ESCAPE_CORRIDOR_DANGER_RADIUS - corridorDistance) * 2.8;
            }

            if (enemy.playerId == assessment.primaryThreat->playerId) {
                score += std::max(0.0, heroThreatDistance + 320.0 - anchorThreatDistance) * 1.4;
            }
        }

        return score;
    }

    void publishTelemetry(const GameSnapshot& snap,
                          const ThreatSummary& summary,
                          std::string_view decision) {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = state_;
        telemetry_.threatLevel = summary.level;
        telemetry_.decision = std::string(decision);
        telemetry_.escapeMap = escapePortalMap_;
        telemetry_.escapePortalId = escapePortalId_;
        telemetry_.visibleEnemies = summary.visibleEnemies;
        telemetry_.closeEnemies = summary.closeEnemies;
        telemetry_.attackers = summary.attackers;
        telemetry_.primaryThreatId = summary.primaryThreatId;
        telemetry_.hpPercent = static_cast<float>(calculateHpPercent(snap));
        telemetry_.primaryThreatDistance = static_cast<float>(summary.primaryThreatDistance);
        telemetry_.fleeTargetX = static_cast<float>(fleeTarget_.x);
        telemetry_.fleeTargetY = static_cast<float>(fleeTarget_.y);
        telemetry_.beingAttacked = summary.beingAttacked;
        telemetry_.adminSeenRecently =
            threatTracker_.adminSeenRecently(
                snap.timestampMs,
                std::max(config_.adminEscapeDelayMs, adminConfig_.escapeDelayMs)
            );
        telemetry_.hostileApproachDetected = summary.hostileApproachDetected;
        telemetry_.fleeRetargets = fleeRetargetCount_;
        telemetry_.lastProgressMs = lastProgressTime_;
    }

    [[nodiscard]] bool isAvoidedMap(const std::string& mapName) const {
        if (mapName.empty()) {
            return false;
        }
        return std::find(mapConfig_.avoidMaps.begin(), mapConfig_.avoidMaps.end(), mapName) !=
               mapConfig_.avoidMaps.end();
    }

    /**
     * @brief Check if a portal target map is unsafe (enemy faction, PvP, or avoided).
     *
     * Faction map prefixes: R=1, E=2, U=3.
     * A map is unsafe if it belongs to a different faction's home territory (X-1..X-4),
     * or is a known PvP map (T-1, G-1), or is in the avoided maps list.
     */
    [[nodiscard]] bool isUnsafePortalTarget(const std::string& targetMapName) const {
        if (targetMapName.empty()) {
            return true;  // Unknown destination = unsafe
        }
        if (isAvoidedMap(targetMapName)) {
            return true;
        }
        // PvP / contested maps are always unsafe for escape
        if (targetMapName == "T-1" || targetMapName == "G-1") {
            return true;
        }
        // Check faction-based safety: only jump to own faction's maps
        const int32_t heroFraction = engine_->hero().fraction;
        if (heroFraction <= 0) {
            return false;  // Unknown faction, can't filter
        }
        // Faction prefixes: 1=R, 2=E, 3=U
        char targetPrefix = targetMapName.empty() ? '\0' : targetMapName[0];
        char ownPrefix = '\0';
        switch (heroFraction) {
            case 1: ownPrefix = 'R'; break;
            case 2: ownPrefix = 'E'; break;
            case 3: ownPrefix = 'U'; break;
            default: return false;
        }
        // Enemy faction home maps (X-1 to X-4) are unsafe
        if (targetPrefix != ownPrefix && (targetPrefix == 'R' || targetPrefix == 'E' || targetPrefix == 'U')) {
            if (targetMapName.size() >= 3 && targetMapName[1] == '-') {
                char mapNum = targetMapName[2];
                if (mapNum >= '1' && mapNum <= '4') {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] MapBounds getMapBounds() const {
        MapBounds bounds;
        const auto info = engine_->mapInfo();
        if (info.width > 0 && info.height > 0) {
            bounds.maxX = static_cast<double>(info.width);
            bounds.maxY = static_cast<double>(info.height);
        }
        return bounds;
    }

    [[nodiscard]] const PortalInfo* findPortalById(const GameSnapshot& snap, int32_t portalId) const {
        for (const auto& portal : snap.entities.portals) {
            if (portal.id == portalId && portal.isWorldPortal()) {
                return &portal;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const StationInfo* findStationById(const GameSnapshot& snap, int32_t stationId) const {
        for (const auto& station : snap.entities.stations) {
            if (station.id == stationId) {
                return &station;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool isPreferredRepairStation(const StationInfo& station) const {
        return station.type == "SPACE_STATION";
    }

    void clearEscapeAnchor() {
        escapeAnchor_.reset();
        escapePortalId_ = 0;
        escapePortalMap_.clear();
        unsafePortalLogged_ = false;
    }

    void clearRepairAnchor() {
        repairAnchor_.reset();
        repairHoldIssued_ = false;
    }

    [[nodiscard]] bool isRepairAnchorReached(const GameSnapshot& snap) const {
        if (snap.inSafeZone) {
            return true;
        }
        if (!repairAnchor_.has_value()) {
            return false;
        }

        const double radius = repairAnchor_->kind == RepairAnchorKind::Station
            ? REPAIR_STATION_ARRIVAL_RADIUS
            : REPAIR_PORTAL_ARRIVAL_RADIUS;
        return Position(snap.hero.x, snap.hero.y).distanceTo(repairAnchor_->position) <= radius;
    }

    [[nodiscard]] bool isEscapeAnchorReached(const GameSnapshot& snap) const {
        if (!escapeAnchor_.has_value()) {
            return false;
        }

        if (escapeAnchor_->kind == RepairAnchorKind::Station && snap.inSafeZone) {
            return true;
        }

        const double radius = escapeAnchor_->kind == RepairAnchorKind::Station
            ? REPAIR_STATION_ARRIVAL_RADIUS
            : REPAIR_PORTAL_ARRIVAL_RADIUS;
        return Position(snap.hero.x, snap.hero.y).distanceTo(escapeAnchor_->position) <= radius;
    }

    [[nodiscard]] std::optional<std::pair<int32_t, const PortalInfo*>>
    findBestEscapePortal(const GameSnapshot& snap, int32_t excludedPortalId = 0) const {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const PortalInfo* bestSafe = nullptr;
        int32_t bestSafeId = 0;
        double bestSafeScore = std::numeric_limits<double>::max();
        const PortalInfo* bestFallback = nullptr;
        int32_t bestFallbackId = 0;
        double bestFallbackScore = std::numeric_limits<double>::max();

        for (const auto& portal : snap.entities.portals) {
            if (!portal.isWorldPortal() || portal.id == excludedPortalId) continue;

            const double score = heroPos.distanceTo(Position(portal.x, portal.y));
            const bool unsafe = isUnsafePortalTarget(portal.targetMapName);

            if (!unsafe && score < bestSafeScore) {
                bestSafeScore = score;
                bestSafe = &portal;
                bestSafeId = portal.id;
            }

            // Fallback: any portal (still useful as physical safe zone anchor)
            if (score < bestFallbackScore) {
                bestFallbackScore = score;
                bestFallback = &portal;
                bestFallbackId = portal.id;
            }
        }

        if (bestSafe) {
            return std::make_pair(bestSafeId, bestSafe);
        }
        if (bestFallback) {
            return std::make_pair(bestFallbackId, bestFallback);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RepairAnchor>
    findBestEscapeAnchor(const GameSnapshot& snap,
                         const ThreatSummary& summary,
                         RepairAnchorKind excludedKind = RepairAnchorKind::None,
                         int32_t excludedId = 0) const {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const auto assessment = assessEnemyFlee(snap, summary);

        std::optional<RepairAnchor> bestAnchor;
        double bestScore = std::numeric_limits<double>::max();
        for (const auto& station : snap.entities.stations) {
            if (!isPreferredRepairStation(station)) {
                continue;
            }
            if (excludedKind == RepairAnchorKind::Station && station.id == excludedId) {
                continue;
            }

            RepairAnchor anchor{
                .kind = RepairAnchorKind::Station,
                .id = station.id,
                .position = Position(station.x, station.y),
                .label = station.name.empty()
                    ? station.type
                    : station.name,
            };
            const double score = scoreEscapeAnchor(heroPos, anchor, assessment);
            if (score < bestScore) {
                bestScore = score;
                bestAnchor = anchor;
            }
        }

        for (const auto& portal : snap.entities.portals) {
            if (!portal.isWorldPortal()) {
                continue;
            }
            if (excludedKind == RepairAnchorKind::Portal && portal.id == excludedId) {
                continue;
            }

            RepairAnchor anchor{
                .kind = RepairAnchorKind::Portal,
                .id = portal.id,
                .position = Position(portal.x, portal.y),
                .label = portal.targetMapName.empty()
                    ? std::string("Portal")
                    : portal.targetMapName,
            };
            const double score = scoreEscapeAnchor(heroPos, anchor, assessment);
            if (score < bestScore) {
                bestScore = score;
                bestAnchor = anchor;
            }
        }

        return bestAnchor;
    }

    [[nodiscard]] std::optional<RepairAnchor> findBestRepairAnchor(const GameSnapshot& snap) const {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const StationInfo* bestPreferredStation = nullptr;
        double bestPreferredStationScore = std::numeric_limits<double>::max();

        for (const auto& station : snap.entities.stations) {
            const double score = heroPos.distanceTo(Position(station.x, station.y));
            if (isPreferredRepairStation(station)) {
                if (score < bestPreferredStationScore) {
                    bestPreferredStationScore = score;
                    bestPreferredStation = &station;
                }
            }
        }

        if (bestPreferredStation) {
            return RepairAnchor{
                .kind = RepairAnchorKind::Station,
                .id = bestPreferredStation->id,
                .position = Position(bestPreferredStation->x, bestPreferredStation->y),
                .label = bestPreferredStation->name.empty()
                    ? bestPreferredStation->type
                    : bestPreferredStation->name,
            };
        }

        if (auto portal = findBestEscapePortal(snap); portal.has_value()) {
            return RepairAnchor{
                .kind = RepairAnchorKind::Portal,
                .id = portal->first,
                .position = Position(portal->second->x, portal->second->y),
                .label = portal->second->targetMapName.empty()
                    ? std::string("Portal")
                    : portal->second->targetMapName,
            };
        }

        return std::nullopt;
    }

    bool selectRepairAnchor(const GameSnapshot& snap) {
        clearRepairAnchor();

        const auto anchor = findBestRepairAnchor(snap);
        if (!anchor.has_value()) {
            fleeTarget_ = Position(snap.hero.x, snap.hero.y);
            escapePortalId_ = 0;
            escapePortalMap_.clear();
            lastFleeDistance_ = 0.0;
            lastProgressTime_ = snap.timestampMs;
            return false;
        }

        repairAnchor_ = anchor;
        repairHoldIssued_ = false;
        fleeTarget_ = repairAnchor_->position;
        escapePortalId_ = repairAnchor_->kind == RepairAnchorKind::Portal ? repairAnchor_->id : 0;
        escapePortalMap_ = repairAnchor_->label;
        beginFleeProgress(snap, Position(snap.hero.x, snap.hero.y).distanceTo(fleeTarget_));
        return true;
    }

    bool selectEscapeAnchor(const GameSnapshot& snap,
                            const ThreatSummary& summary,
                            RepairAnchorKind excludedKind = RepairAnchorKind::None,
                            int32_t excludedId = 0) {
        clearEscapeAnchor();

        const auto anchor = findBestEscapeAnchor(snap, summary, excludedKind, excludedId);
        if (!anchor.has_value()) {
            fleeTarget_ = Position(snap.hero.x, snap.hero.y);
            lastFleeDistance_ = 0.0;
            lastProgressTime_ = snap.timestampMs;
            return false;
        }

        escapeAnchor_ = anchor;
        fleeTarget_ = escapeAnchor_->position;
        escapePortalId_ = escapeAnchor_->kind == RepairAnchorKind::Portal ? escapeAnchor_->id : 0;
        escapePortalMap_ = escapeAnchor_->label;
        beginFleeProgress(snap, Position(snap.hero.x, snap.hero.y).distanceTo(fleeTarget_));
        return true;
    }

    void beginFleeProgress(const GameSnapshot& snap, double initialDistance) {
        lastFleeDistance_ = initialDistance;
        lastProgressTime_ = snap.timestampMs;
    }

    void selectFallbackFleeTarget(const GameSnapshot& snap,
                                  const ThreatSummary& summary) {
        clearEscapeAnchor();

        const auto assessment = assessEnemyFlee(snap, summary);
        const EnemyInfo* threat = assessment.primaryThreat.has_value()
            ? &(*assessment.primaryThreat)
            : threatTracker_.getPrimaryThreat(
                snap.hero.x,
                snap.hero.y,
                snap.timestampMs,
                config_.enemySeenTimeoutMs
            );

        const MapBounds bounds = getMapBounds();
        if (threat) {
            fleeTarget_ = navigation_.fleeFromPosition(
                Position(snap.hero.x, snap.hero.y),
                Position(threat->lastX, threat->lastY),
                bounds
            );
        } else {
            fleeTarget_ = navigation_.randomPosition(bounds, 600);
        }

        beginFleeProgress(snap, Position(snap.hero.x, snap.hero.y).distanceTo(fleeTarget_));
    }

    void startFleeing(const GameSnapshot& snap,
                      const ThreatSummary& summary,
                      std::string_view reason = "Threat") {
        transitionTo(SafetyState::Fleeing, snap.timestampMs);
        clearRepairAnchor();

        if (snap.inSafeZone) {
            clearEscapeAnchor();
            fleeTarget_ = Position(snap.hero.x, snap.hero.y);
            beginFleeProgress(snap, 0.0);
            transitionTo(SafetyState::AtPortal, snap.timestampMs);
            publishTelemetry(snap, summary, "AlreadySafeZone");
            return;
        }

        if (config_.useEscapeConfig && snap.hero.activeConfig != config_.escapeConfigId) {
            switchConfig(config_.escapeConfigId);
        }

        if (config_.preferPortalEscape) {
            if (selectEscapeAnchor(snap, summary)) {
                std::cout << "[Safety] Fleeing to " << repairAnchorKindName(escapeAnchor_->kind)
                          << " at (" << fleeTarget_.x << ", " << fleeTarget_.y << ")\n";
                publishTelemetry(snap, summary, reason);
                return;
            }
        }

        selectFallbackFleeTarget(snap, summary);
        std::cout << "[Safety] Fleeing to (" << fleeTarget_.x << ", " << fleeTarget_.y << ")\n";
        publishTelemetry(snap, summary, reason);
    }

    void startRepairing(const GameSnapshot& snap,
                        const ThreatSummary& summary,
                        std::string_view reason = "Repairing") {
        transitionTo(SafetyState::Repairing, snap.timestampMs);
        if (movement_) {
            movement_->release(name());
        }
        repairHoldIssued_ = false;

        if (config_.useEscapeConfig && snap.hero.activeConfig != config_.escapeConfigId) {
            switchConfig(config_.escapeConfigId);
        }

        if (snap.inSafeZone) {
            clearRepairAnchor();
            fleeTarget_ = Position(snap.hero.x, snap.hero.y);
            lastProgressTime_ = snap.timestampMs;
            std::cout << "[Safety] HP low, holding repair position inside safe zone\n";
            publishTelemetry(snap, summary, reason);
            return;
        }

        if (selectRepairAnchor(snap)) {
            const auto anchorKind = repairAnchor_->kind == RepairAnchorKind::Station
                ? "station"
                : "portal";
            std::cout << "[Safety] HP low, moving to repair " << anchorKind
                      << " at (" << repairAnchor_->position.x << ", "
                      << repairAnchor_->position.y << ")\n";
        } else {
            std::cout << "[Safety] HP low, no repair anchor found - holding current position\n";
        }

        publishTelemetry(snap, summary, reason);
    }

    void retargetEscape(const GameSnapshot& snap,
                        const ThreatSummary& summary,
                        std::string_view reason) {
        if (movement_) {
            movement_->release(name());
        }

        if (escapeAnchor_.has_value()) {
            if (selectEscapeAnchor(snap, summary, escapeAnchor_->kind, escapeAnchor_->id)) {
                fleeRetargetCount_++;
                std::cout << "[Safety] Retargeting escape "
                          << repairAnchorKindName(escapeAnchor_->kind)
                          << " to " << escapePortalMap_ << "\n";
                publishTelemetry(snap, summary, reason);
                return;
            }
        }

        fleeRetargetCount_++;
        selectFallbackFleeTarget(snap, summary);
        publishTelemetry(snap, summary, reason);
    }

    void handleMonitoring(const GameSnapshot& snap,
                          double hpPercent,
                          const ThreatSummary& summary) {
        const auto assessment = assessEnemyFlee(snap, summary);
        if (assessment.active || hpPercent < config_.minHpPercent) {
            startFleeing(snap, summary, "StartFlee");
            return;
        }

        if (hpPercent < config_.repairHpPercent) {
            startRepairing(snap, summary, "Repairing");
            return;
        }

        publishTelemetry(snap, summary, "Monitoring");
    }

    void handleFleeing(const GameSnapshot& snap,
                       double hpPercent,
                       const ThreatSummary& summary) {
        const auto assessment = assessEnemyFlee(snap, summary);
        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);

        if (escapeAnchor_.has_value()) {
            if (escapeAnchor_->kind == RepairAnchorKind::Portal) {
                if (const auto* portal = findPortalById(snap, escapeAnchor_->id)) {
                    fleeTarget_ = Position(portal->x, portal->y);
                    escapePortalId_ = portal->id;
                    escapePortalMap_ = portal->targetMapName.empty()
                        ? std::string("Portal")
                        : portal->targetMapName;
                } else {
                    retargetEscape(snap, summary, "PortalLost");
                    return;
                }
            } else if (escapeAnchor_->kind == RepairAnchorKind::Station) {
                if (const auto* station = findStationById(snap, escapeAnchor_->id)) {
                    fleeTarget_ = Position(station->x, station->y);
                    escapePortalId_ = 0;
                    escapePortalMap_ = station->name.empty()
                        ? station->type
                        : station->name;
                } else {
                    retargetEscape(snap, summary, "StationLost");
                    return;
                }
            } else {
                clearEscapeAnchor();
            }
        }

        const double distToTarget = heroPos.distanceTo(fleeTarget_);
        if (distToTarget + 60.0 < lastFleeDistance_) {
            lastFleeDistance_ = distToTarget;
            lastProgressTime_ = now;
        }

        if (escapeAnchor_.has_value() && isEscapeAnchorReached(snap)) {
            transitionTo(SafetyState::AtPortal, now);
            std::cout << "[Safety] Reached escape "
                      << repairAnchorKindName(escapeAnchor_->kind) << "\n";
            publishTelemetry(
                snap,
                summary,
                escapeAnchor_->kind == RepairAnchorKind::Station ? "AtStation" : "AtPortal"
            );
            return;
        }

        if (!assessment.active && hpPercent >= config_.repairHpPercent) {
            transitionTo(SafetyState::Monitoring, now);
            if (movement_) {
                movement_->release(name());
            }
            std::cout << "[Safety] Threat cleared, returning to monitoring\n";
            publishTelemetry(snap, summary, "ThreatCleared");
            return;
        }

        if (lastProgressTime_ > 0 && now - lastProgressTime_ > FLEE_STALL_TIMEOUT_MS) {
            retargetEscape(snap, summary, "FleeStalled");
            return;
        }

        if (now - lastMoveTime_ >= config_.fleeMoveCooldownMs) {
            lastMoveTime_ = now;

            if (!escapeAnchor_.has_value()) {
                const EnemyInfo* threat = assessment.primaryThreat.has_value()
                    ? &(*assessment.primaryThreat)
                    : threatTracker_.getPrimaryThreat(
                        snap.hero.x,
                        snap.hero.y,
                        snap.timestampMs,
                        config_.enemySeenTimeoutMs
                    );

                if (threat) {
                    fleeTarget_ = navigation_.fleeFromPosition(
                        Position(snap.hero.x, snap.hero.y),
                        Position(threat->lastX, threat->lastY),
                        getMapBounds()
                    );
                }
            }

            if (movement_) {
                movement_->move(name(), snap, fleeTarget_, MoveIntent::Escape);
            } else {
                engine_->moveTo(static_cast<float>(fleeTarget_.x),
                                static_cast<float>(fleeTarget_.y));
            }
        }

        publishTelemetry(
            snap,
            summary,
            escapeAnchor_.has_value()
                ? (escapeAnchor_->kind == RepairAnchorKind::Station ? "FleeStation" : "FleePortal")
                : "FleeOpenSpace"
        );
    }

    void handleAtPortal(const GameSnapshot& snap,
                        double hpPercent,
                        const ThreatSummary& summary) {
        const auto assessment = assessEnemyFlee(snap, summary);
        const bool holdingStation =
            escapeAnchor_.has_value() && escapeAnchor_->kind == RepairAnchorKind::Station;
        const bool atPortal =
            escapeAnchor_.has_value() &&
            escapeAnchor_->kind == RepairAnchorKind::Portal;
        const bool portalIsSafe = atPortal &&
            !escapePortalMap_.empty() &&
            !isUnsafePortalTarget(escapePortalMap_);

        if (atPortal && portalIsSafe) {
            std::cout << "[Safety] Jumping through portal to " << escapePortalMap_ << "\n";
            engine_->teleport();
            // Hold in AwaitingTeleport until map actually changes
            teleportFromMap_ = snap.mapName;
            teleportSentAtMs_ = snap.timestampMs;
            if (movement_) {
                movement_->release(name());
            }
            engine_->clearPendingActions();
            transitionTo(SafetyState::AwaitingTeleport, snap.timestampMs);
            publishTelemetry(snap, summary, "PortalJump");
            return;
        }

        // Log once when we first discover the portal is unsafe
        if (atPortal && !portalIsSafe && !unsafePortalLogged_) {
            std::cout << "[Safety] Portal to " << escapePortalMap_
                      << " is unsafe, holding position\n";
            unsafePortalLogged_ = true;
        }

        if (!assessment.active && hpPercent >= config_.fullHpPercent) {
            if (movement_) {
                movement_->release(name());
            }
            transitionTo(SafetyState::Monitoring, snap.timestampMs);
            clearEscapeAnchor();
            std::cout << "[Safety] Safe at anchor, returning to normal operation\n";
            publishTelemetry(
                snap,
                summary,
                holdingStation ? "SafeAtStation" : "SafeAtPortal"
            );
            return;
        }

        publishTelemetry(
            snap,
            summary,
            escapeAnchor_.has_value()
                ? (holdingStation ? "HoldingStation" : "HoldingPortal")
                : "HoldingSafeZone"
        );
    }

    void handleAwaitingTeleport(const GameSnapshot& snap,
                                double /*hpPercent*/,
                                const ThreatSummary& summary) {
        // Map changed = teleport completed
        if (snap.mapName != teleportFromMap_ && !snap.mapName.empty()) {
            std::cout << "[Safety] Teleport complete: " << teleportFromMap_
                      << " -> " << snap.mapName << "\n";
            threatTracker_.clear();
            clearEscapeAnchor();
            fleeTarget_ = Position{};
            lastFleeDistance_ = std::numeric_limits<double>::max();
            fleeRetargetCount_ = 0;
            teleportFromMap_.clear();
            teleportSentAtMs_ = 0;
            transitionTo(SafetyState::Monitoring, snap.timestampMs);
            publishTelemetry(snap, summary, "TeleportComplete");
            return;
        }

        // Timeout — teleport didn't happen (maybe moved away from portal)
        if (snap.timestampMs - teleportSentAtMs_ > TELEPORT_TIMEOUT_MS) {
            std::cout << "[Safety] Teleport timed out, returning to monitoring\n";
            teleportFromMap_.clear();
            teleportSentAtMs_ = 0;
            transitionTo(SafetyState::Monitoring, snap.timestampMs);
            publishTelemetry(snap, summary, "TeleportTimeout");
            return;
        }

        // While waiting: don't issue any movement, just hold
        publishTelemetry(snap, summary, "AwaitingTeleport");
    }

    void handleRepairing(const GameSnapshot& snap,
                         double hpPercent,
                         const ThreatSummary& summary) {
        const auto assessment = assessEnemyFlee(snap, summary);
        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);

        if (hpPercent >= config_.fullHpPercent) {
            if (movement_) {
                movement_->release(name());
            }
            clearRepairAnchor();
            transitionTo(SafetyState::Monitoring, snap.timestampMs);
            std::cout << "[Safety] Repair complete (" << hpPercent << "%)\n";

            if (config_.useEscapeConfig && snap.hero.activeConfig != config_.fightConfigId) {
                switchConfig(config_.fightConfigId);
            }

            publishTelemetry(snap, summary, "RepairComplete");
            return;
        }

        if (assessment.active &&
            !(reviveGraceUntilMs_ > 0 && now < reviveGraceUntilMs_)) {
            clearRepairAnchor();
            startFleeing(snap, summary, "RepairThreat");
            return;
        }

        if (config_.useEscapeConfig && snap.hero.activeConfig != config_.escapeConfigId) {
            switchConfig(config_.escapeConfigId);
        }

        // During revive grace period, treat as safe zone — hold position
        // until server confirms inSafeZone (avoids flying away to portal)
        const bool effectiveSafeZone = snap.inSafeZone ||
            (reviveGraceUntilMs_ > 0 && now < reviveGraceUntilMs_);

        // Clear grace once server confirms safe zone
        if (snap.inSafeZone && reviveGraceUntilMs_ > 0) {
            reviveGraceUntilMs_ = 0;
        }

        if (!effectiveSafeZone) {
            bool reacquireAnchor = false;
            if (!repairAnchor_.has_value()) {
                reacquireAnchor = true;
            } else if (repairAnchor_->kind == RepairAnchorKind::Portal &&
                       findPortalById(snap, repairAnchor_->id) == nullptr) {
                reacquireAnchor = true;
            } else if (repairAnchor_->kind == RepairAnchorKind::Station &&
                       findStationById(snap, repairAnchor_->id) == nullptr) {
                reacquireAnchor = true;
            }

            if (reacquireAnchor && selectRepairAnchor(snap)) {
                const auto anchorKind = repairAnchor_->kind == RepairAnchorKind::Station
                    ? "station"
                    : "portal";
                std::cout << "[Safety] Reacquired repair " << anchorKind
                          << " at (" << repairAnchor_->position.x << ", "
                          << repairAnchor_->position.y << ")\n";
            }
        }

        if (repairAnchor_.has_value() && !isRepairAnchorReached(snap)) {
            const double distToAnchor = heroPos.distanceTo(repairAnchor_->position);
            if (distToAnchor + REPAIR_PROGRESS_DELTA < lastFleeDistance_) {
                lastFleeDistance_ = distToAnchor;
                lastProgressTime_ = now;
            }

            if (lastProgressTime_ > 0 && now - lastProgressTime_ > REPAIR_STALL_TIMEOUT_MS) {
                if (selectRepairAnchor(snap)) {
                    std::cout << "[Safety] Repair path stalled, retargeting anchor\n";
                } else {
                    std::cout << "[Safety] Repair path stalled, no alternate anchor found\n";
                }
            }

            if (now - lastMoveTime_ >= config_.fleeMoveCooldownMs) {
                lastMoveTime_ = now;
                if (movement_) {
                    movement_->move(name(), snap, repairAnchor_->position, MoveIntent::Escape);
                } else {
                    engine_->moveTo(static_cast<float>(repairAnchor_->position.x),
                                    static_cast<float>(repairAnchor_->position.y));
                }
            }

            publishTelemetry(
                snap,
                summary,
                repairAnchor_->kind == RepairAnchorKind::Station
                    ? "MoveRepairStation"
                    : "MoveRepairPortal"
            );
            return;
        }

        if (movement_) {
            movement_->release(name());
        }

        const Position holdPos = repairAnchor_.has_value()
            ? repairAnchor_->position
            : Position(snap.hero.x, snap.hero.y);
        if (!repairHoldIssued_ ||
            (snap.hero.isMoving && now - lastMoveTime_ >= REPAIR_HOLD_REISSUE_MS)) {
            engine_->moveTo(static_cast<float>(holdPos.x), static_cast<float>(holdPos.y));
            repairHoldIssued_ = true;
            lastMoveTime_ = now;
        }

        publishTelemetry(
            snap,
            summary,
            snap.inSafeZone ? "RepairSafeZone" : "RepairHoldingAnchor"
        );
    }

    void handleConfigSwitching(const GameSnapshot& snap,
                               const ThreatSummary& summary) {
        transitionTo(SafetyState::Monitoring, snap.timestampMs);
        publishTelemetry(snap, summary, "ConfigSwitch");
    }

    void switchConfig(int32_t configId) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count() / 1000000;
        if (now - lastConfigSwitchTime_ < config_.configSwitchCooldownMs) {
            return;
        }

        lastConfigSwitchTime_ = now;
        engine_->switchConfig(configId);
        std::cout << "[Safety] Switching to config " << configId << "\n";
    }
};

} // namespace dynamo
