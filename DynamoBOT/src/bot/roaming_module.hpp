#pragma once

/**
 * @file roaming_module.hpp
 * @brief Roaming module - map-aware idle movement
 *
 * Lowest priority module, keeps the bot moving when nothing else to do.
 * War Universe maps do not need obstacle pathing like DarkOrbit, so roaming
 * focuses on chunk-based target selection, portal avoidance, and anti-stuck
 * recovery instead of obstacle-aware routing.
 */

#include "module.hpp"
#include "bot_config.hpp"
#include "navigation.hpp"
#include "movement_controller.hpp"
#include "../game/game_engine.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

namespace dynamo {

struct RoamingTelemetry {
    std::string decision{"Idle"};
    float anchorX{0.0f};
    float anchorY{0.0f};
    float destinationX{0.0f};
    float destinationY{0.0f};
    bool hasDestination{false};
    int32_t legsCompleted{0};
    int32_t recoveries{0};
    int32_t zoneId{-1};
    int32_t ignoredZones{0};
    int32_t failedSpots{0};
    int64_t dwellUntilMs{0};
    int64_t lastProgressMs{0};
    bool mapReady{false};
};

/**
 * @brief Roaming module for idle movement
 *
 * Responsibilities:
 * - Move semi-randomly around the real map when no other tasks exist
 * - Stay away from map edges and portal hotspots
 * - Reuse work-area-like chunks instead of picking points from the whole map
 * - Blacklist stuck zones and destinations for a while
 */
class RoamingModule : public Module {
public:
    RoamingModule(std::shared_ptr<GameEngine> engine,
                  std::shared_ptr<MovementController> movement,
                  const RoamingConfig& config)
        : Module(std::move(engine), std::move(movement))
        , config_(config) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Roaming";
    }

    [[nodiscard]] int getPriority(const GameSnapshot& /*snap*/) override {
        if (!config_.enabled || !enabled_) {
            return 0;
        }
        return hasUsableMapInfo() ? config_.priority : 0;
    }

    void onStart() override {
        resetTransientState(false);
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_ = RoamingTelemetry{};
        }
        std::cout << "[Roaming] Module activated - idle movement\n";
    }

    void onStop() override {
        resetTransientState(true);
        publishTelemetry("Stopped");
        std::cout << "[Roaming] Module deactivated\n";
    }

    void tick(const GameSnapshot& snap) override {
        ensureRoamingConfig(snap);
        pruneFailureState(snap.timestampMs);

        if (snap.mapId != 0 && snap.mapId != lastMapId_) {
            clearFailureState();
            resetTransientState(true);
            lastMapId_ = snap.mapId;
        } else if (lastMapId_ == 0 && snap.mapId != 0) {
            lastMapId_ = snap.mapId;
        }

        if (!hasUsableMapInfo()) {
            hasDestination_ = false;
            hasAnchor_ = false;
            publishTelemetry("AwaitMapInfo");
            return;
        }

        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);

        if (!hasDestination_) {
            if (!pickNewDestination(snap, false)) {
                publishTelemetry("NoRoamZone");
                return;
            }
            issueMoveToDestination(snap);
            publishTelemetry("PickDestination");
            return;
        }

        const double distToDestination = heroPos.distanceTo(destination_);

        if (distToDestination < ARRIVAL_DISTANCE) {
            legsCompleted_++;
            clearZoneFailure(currentZoneId_);
            if (!pickNewDestination(snap, false)) {
                hasDestination_ = false;
                publishTelemetry("NoRoamZone");
                return;
            }
            issueMoveToDestination(snap);
            publishTelemetry("ContinueRoam");
            return;
        }

        updateProgressSample(snap, distToDestination);
        if (lastProgressTime_ > 0 &&
            now - lastProgressTime_ > std::max<int64_t>(4500, config_.moveCooldownMs * 3LL)) {
            recoveries_++;
            registerFailure(now, "StuckRecover");
            if (!pickNewDestination(snap, true)) {
                publishTelemetry("RecoverNoZone");
                return;
            }
            issueMoveToDestination(snap);
            publishTelemetry("StuckRecover");
            return;
        }

        if (movement_) {
            movement_->move(name(), snap, destination_, MoveIntent::Roam);
        } else if (now - lastMoveTime_ >= config_.moveCooldownMs) {
            lastMoveTime_ = now;
            engine_->moveTo(static_cast<float>(destination_.x),
                            static_cast<float>(destination_.y));
        }

        publishTelemetry("Roaming");
    }

    [[nodiscard]] RoamingTelemetry getTelemetry() const {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        return telemetry_;
    }

private:
    struct RoamZone {
        int32_t id{0};
        double minX{0.0};
        double minY{0.0};
        double maxX{0.0};
        double maxY{0.0};

        [[nodiscard]] double width() const { return maxX - minX; }
        [[nodiscard]] double height() const { return maxY - minY; }

        [[nodiscard]] Position center() const {
            return Position((minX + maxX) * 0.5, (minY + maxY) * 0.5);
        }

        [[nodiscard]] Position sample(std::mt19937& rng, double padding) const {
            const double innerMinX = std::min(maxX, minX + padding);
            const double innerMaxX = std::max(minX, maxX - padding);
            const double innerMinY = std::min(maxY, minY + padding);
            const double innerMaxY = std::max(minY, maxY - padding);

            std::uniform_real_distribution<double> distX(innerMinX, innerMaxX);
            std::uniform_real_distribution<double> distY(innerMinY, innerMaxY);
            return Position(distX(rng), distY(rng));
        }
    };

    struct ZoneFailureState {
        int32_t failures{0};
        int64_t ignoreUntilMs{0};
    };

    struct FailedSpot {
        Position position;
        double radius{0.0};
        int64_t ignoreUntilMs{0};
    };

    RoamingConfig config_;
    Navigation navigation_;

    Position anchor_;
    Position destination_;
    Position lastHeroPos_;
    bool hasDestination_{false};
    bool hasAnchor_{false};
    int32_t currentZoneId_{-1};
    int64_t lastMoveTime_{0};
    int64_t lastConfigSwitchTime_{0};
    int64_t dwellUntilMs_{0};
    int64_t lastProgressTime_{0};
    int32_t lastMapId_{0};
    int32_t legsCompleted_{0};
    int32_t recoveries_{0};
    double lastDistanceToDestination_{std::numeric_limits<double>::max()};
    std::unordered_map<int32_t, ZoneFailureState> ignoredZones_;
    std::vector<FailedSpot> failedSpots_;
    mutable std::mutex telemetryMutex_;
    RoamingTelemetry telemetry_;
    mutable std::mt19937 rng_{std::random_device{}()};

    static constexpr int64_t CONFIG_SWITCH_COOLDOWN_MS = 1000;
    static constexpr int32_t GRID_COLUMNS = 5;
    static constexpr int32_t GRID_ROWS = 4;
    static constexpr double ARRIVAL_DISTANCE = 320.0;
    static constexpr double MIN_DESTINATION_DISTANCE = 420.0;
    static constexpr double RECOVERY_MIN_DESTINATION_DISTANCE = 900.0;
    static constexpr double PORTAL_AVOID_DISTANCE = 950.0;
    static constexpr double PORTAL_ZONE_PADDING = 180.0;
    static constexpr double FAILED_SPOT_RADIUS = 750.0;
    static constexpr int64_t FAILED_SPOT_IGNORE_MS = 60000;
    static constexpr int64_t ZONE_IGNORE_MS = 120000;
    static constexpr int32_t MAX_ZONE_FAILURES = 2;

    void resetTransientState(bool releaseMovement) {
        if (releaseMovement && movement_) {
            movement_->release(name());
        }
        hasDestination_ = false;
        hasAnchor_ = false;
        currentZoneId_ = -1;
        lastMoveTime_ = 0;
        dwellUntilMs_ = 0;
        lastProgressTime_ = 0;
        lastDistanceToDestination_ = std::numeric_limits<double>::max();
    }

    void clearFailureState() {
        ignoredZones_.clear();
        failedSpots_.clear();
    }

    void ensureRoamingConfig(const GameSnapshot& snap) {
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

    [[nodiscard]] bool hasUsableMapInfo() const {
        const auto info = engine_->mapInfo();
        return info.width > 0 &&
               info.height > 0 &&
               (info.id != 0 || !info.name.empty());
    }

    [[nodiscard]] std::optional<MapBounds> getMapBounds() const {
        const auto info = engine_->mapInfo();
        if (info.width <= 0 || info.height <= 0) {
            return std::nullopt;
        }

        MapBounds bounds;
        bounds.maxX = static_cast<double>(info.width);
        bounds.maxY = static_cast<double>(info.height);
        return bounds;
    }

    static double distanceToRect(const Position& point, const RoamZone& zone) {
        const double dx = std::max({zone.minX - point.x, 0.0, point.x - zone.maxX});
        const double dy = std::max({zone.minY - point.y, 0.0, point.y - zone.maxY});
        return std::sqrt(dx * dx + dy * dy);
    }

    [[nodiscard]] bool isZoneIgnored(int32_t zoneId, int64_t now) const {
        auto it = ignoredZones_.find(zoneId);
        return it != ignoredZones_.end() && it->second.ignoreUntilMs > now;
    }

    void clearZoneFailure(int32_t zoneId) {
        if (zoneId < 0) {
            return;
        }
        ignoredZones_.erase(zoneId);
    }

    void pruneFailureState(int64_t now) {
        for (auto it = ignoredZones_.begin(); it != ignoredZones_.end();) {
            if (it->second.ignoreUntilMs > 0 && it->second.ignoreUntilMs <= now) {
                it = ignoredZones_.erase(it);
            } else {
                ++it;
            }
        }

        failedSpots_.erase(
            std::remove_if(
                failedSpots_.begin(),
                failedSpots_.end(),
                [now](const FailedSpot& spot) { return spot.ignoreUntilMs <= now; }
            ),
            failedSpots_.end()
        );
    }

    void publishTelemetry(std::string_view decision) {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.decision = std::string(decision);
        telemetry_.anchorX = static_cast<float>(anchor_.x);
        telemetry_.anchorY = static_cast<float>(anchor_.y);
        telemetry_.destinationX = static_cast<float>(destination_.x);
        telemetry_.destinationY = static_cast<float>(destination_.y);
        telemetry_.hasDestination = hasDestination_;
        telemetry_.legsCompleted = legsCompleted_;
        telemetry_.recoveries = recoveries_;
        telemetry_.zoneId = currentZoneId_;
        telemetry_.ignoredZones = static_cast<int32_t>(ignoredZones_.size());
        telemetry_.failedSpots = static_cast<int32_t>(failedSpots_.size());
        telemetry_.dwellUntilMs = dwellUntilMs_;
        telemetry_.lastProgressMs = lastProgressTime_;
        telemetry_.mapReady = hasUsableMapInfo();
    }

    void updateProgressSample(const GameSnapshot& snap, double distanceToDestination) {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const double heroDelta = heroPos.distanceTo(lastHeroPos_);

        if (distanceToDestination + 70.0 < lastDistanceToDestination_ || heroDelta > 55.0) {
            lastProgressTime_ = snap.timestampMs;
        }

        lastDistanceToDestination_ = distanceToDestination;
        lastHeroPos_ = heroPos;
    }

    void issueMoveToDestination(const GameSnapshot& snap) {
        if (!hasDestination_) {
            return;
        }

        lastMoveTime_ = snap.timestampMs;
        if (movement_) {
            movement_->move(name(), snap, destination_, MoveIntent::Roam);
        } else {
            engine_->moveTo(static_cast<float>(destination_.x),
                            static_cast<float>(destination_.y));
        }
    }

    void registerFailure(int64_t now, std::string_view reason) {
        if (movement_) {
            movement_->release(name());
        }

        failedSpots_.push_back(FailedSpot{destination_, FAILED_SPOT_RADIUS, now + FAILED_SPOT_IGNORE_MS});

        if (currentZoneId_ >= 0) {
            auto& state = ignoredZones_[currentZoneId_];
            state.failures++;
            if (state.failures >= MAX_ZONE_FAILURES) {
                state.ignoreUntilMs = now + ZONE_IGNORE_MS;
                state.failures = 0;
                std::cout << "[Roaming] " << reason << " - ignoring zone "
                          << currentZoneId_ << " for " << (ZONE_IGNORE_MS / 1000) << "s\n";
                currentZoneId_ = -1;
                hasAnchor_ = false;
            } else {
                std::cout << "[Roaming] " << reason << " in zone " << currentZoneId_
                          << " (failure " << state.failures << "/" << MAX_ZONE_FAILURES << ")\n";
            }
        } else {
            std::cout << "[Roaming] " << reason << "\n";
        }

        hasDestination_ = false;
        dwellUntilMs_ = 0;
        lastDistanceToDestination_ = std::numeric_limits<double>::max();
    }

    [[nodiscard]] double nearestPortalDistance(const Position& point, const GameSnapshot& snap) const {
        double nearest = std::numeric_limits<double>::max();
        for (const auto& portal : snap.entities.portals) {
            if (!portal.active) {
                continue;
            }
            const double dist = point.distanceTo(Position(portal.x, portal.y));
            nearest = std::min(nearest, dist);
        }
        return nearest;
    }

    [[nodiscard]] bool isFailedSpot(const Position& point, int64_t now) const {
        for (const auto& spot : failedSpots_) {
            if (spot.ignoreUntilMs <= now) {
                continue;
            }
            if (spot.position.distanceTo(point) <= spot.radius) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool isUsablePoint(const Position& point,
                                     const GameSnapshot& snap,
                                     const MapBounds& bounds,
                                     int64_t now) const {
        if (!bounds.contains(point, config_.mapMargin)) {
            return false;
        }
        if (nearestPortalDistance(point, snap) < PORTAL_AVOID_DISTANCE) {
            return false;
        }
        if (isFailedSpot(point, now)) {
            return false;
        }
        return true;
    }

    [[nodiscard]] std::vector<RoamZone> buildRoamZones(const MapBounds& bounds) const {
        std::vector<RoamZone> zones;

        const double margin = std::max(350.0, static_cast<double>(config_.mapMargin));
        const double innerMinX = bounds.minX + margin;
        const double innerMinY = bounds.minY + margin;
        const double innerMaxX = bounds.maxX - margin;
        const double innerMaxY = bounds.maxY - margin;

        const double innerWidth = innerMaxX - innerMinX;
        const double innerHeight = innerMaxY - innerMinY;
        if (innerWidth < 600.0 || innerHeight < 600.0) {
            zones.push_back(RoamZone{0, innerMinX, innerMinY, innerMaxX, innerMaxY});
            return zones;
        }

        const int columns = innerWidth < 5000.0 ? 3 : GRID_COLUMNS;
        const int rows = innerHeight < 4000.0 ? 3 : GRID_ROWS;
        const double zoneWidth = innerWidth / columns;
        const double zoneHeight = innerHeight / rows;

        int32_t zoneId = 0;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                RoamZone zone;
                zone.id = zoneId++;
                zone.minX = innerMinX + x * zoneWidth;
                zone.maxX = innerMinX + (x + 1) * zoneWidth;
                zone.minY = innerMinY + y * zoneHeight;
                zone.maxY = innerMinY + (y + 1) * zoneHeight;
                zones.push_back(zone);
            }
        }

        return zones;
    }

    [[nodiscard]] bool isPortalFriendlyZone(const RoamZone& zone, const GameSnapshot& snap) const {
        for (const auto& portal : snap.entities.portals) {
            if (!portal.active) {
                continue;
            }

            const Position portalPos(portal.x, portal.y);
            if (distanceToRect(portalPos, zone) < PORTAL_ZONE_PADDING) {
                return false;
            }
            if (portalPos.distanceTo(zone.center()) < PORTAL_AVOID_DISTANCE) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<RoamZone> findZoneById(const std::vector<RoamZone>& zones,
                                                       int32_t zoneId) const {
        for (const auto& zone : zones) {
            if (zone.id == zoneId) {
                return zone;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RoamZone> chooseZone(const GameSnapshot& snap,
                                                     const std::vector<RoamZone>& zones,
                                                     bool recovery) {
        if (zones.empty()) {
            return std::nullopt;
        }

        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);
        std::vector<RoamZone> preferredZones;
        std::vector<RoamZone> fallbackZones;

        for (const auto& zone : zones) {
            if (isZoneIgnored(zone.id, now)) {
                continue;
            }
            fallbackZones.push_back(zone);
            if (isPortalFriendlyZone(zone, snap)) {
                preferredZones.push_back(zone);
            }
        }

        std::vector<RoamZone>* candidateZones = &preferredZones;
        if (candidateZones->empty()) {
            candidateZones = &fallbackZones;
        }
        if (candidateZones->empty()) {
            return std::nullopt;
        }

        const bool keepCurrentZone =
            !recovery &&
            currentZoneId_ >= 0 &&
            legsCompleted_ % 3 != 0 &&
            std::bernoulli_distribution(0.65)(rng_);

        if (keepCurrentZone) {
            if (auto current = findZoneById(*candidateZones, currentZoneId_); current.has_value()) {
                return current;
            }
        }

        const bool longHop = recovery || std::bernoulli_distribution(0.22)(rng_);
        const double desiredDistance = longHop ? 4200.0 : 2200.0;

        const RoamZone* bestZone = nullptr;
        double bestScore = -std::numeric_limits<double>::infinity();
        std::uniform_real_distribution<double> jitter(0.0, 450.0);

        for (const auto& zone : *candidateZones) {
            const double heroDistance = heroPos.distanceTo(zone.center());
            double score = 0.0;

            if (recovery) {
                score = heroDistance;
                if (zone.id != currentZoneId_) {
                    score += 400.0;
                }
            } else if (longHop) {
                score = heroDistance;
            } else {
                score = -std::abs(heroDistance - desiredDistance);
                if (zone.id == currentZoneId_) {
                    score += 220.0;
                }
            }

            score += jitter(rng_);

            if (score > bestScore) {
                bestScore = score;
                bestZone = &zone;
            }
        }

        if (bestZone) {
            return *bestZone;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Position> chooseDestinationInZone(const RoamZone& zone,
                                                                  const GameSnapshot& snap,
                                                                  const MapBounds& bounds,
                                                                  bool recovery) {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const auto now = snap.timestampMs;
        const double zonePadding = std::clamp(std::min(zone.width(), zone.height()) * 0.18, 120.0, 320.0);
        const double minDistance = recovery ? RECOVERY_MIN_DESTINATION_DISTANCE : MIN_DESTINATION_DISTANCE;

        for (int attempt = 0; attempt < 32; ++attempt) {
            const Position candidate = zone.sample(rng_, zonePadding);
            if (heroPos.distanceTo(candidate) < minDistance) {
                continue;
            }
            if (!isUsablePoint(candidate, snap, bounds, now)) {
                continue;
            }
            return candidate;
        }

        const Position zoneCenter = zone.center();
        if (heroPos.distanceTo(zoneCenter) >= minDistance &&
            isUsablePoint(zoneCenter, snap, bounds, now)) {
            return zoneCenter;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<Position> pickEmergencyDestination(const GameSnapshot& snap,
                                                                   const MapBounds& bounds) {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const auto now = snap.timestampMs;
        const double fallbackMinDistance = std::max(320.0, MIN_DESTINATION_DISTANCE * 0.6);

        for (int attempt = 0; attempt < 36; ++attempt) {
            const Position candidate = navigation_.randomPosition(
                bounds,
                std::max(260.0, static_cast<double>(config_.mapMargin))
            );
            if (heroPos.distanceTo(candidate) < fallbackMinDistance) {
                continue;
            }
            if (!bounds.contains(candidate, config_.mapMargin)) {
                continue;
            }
            if (nearestPortalDistance(candidate, snap) < PORTAL_AVOID_DISTANCE * 0.65) {
                continue;
            }
            if (isFailedSpot(candidate, now) && attempt < 28) {
                continue;
            }
            return candidate;
        }

        const Position center = bounds.center();
        if (heroPos.distanceTo(center) >= 220.0) {
            return bounds.clamp(center, std::max(220.0, static_cast<double>(config_.mapMargin)));
        }

        Position drift = heroPos.offset(1200.0, 700.0);
        return bounds.clamp(drift, std::max(220.0, static_cast<double>(config_.mapMargin)));
    }

    [[nodiscard]] bool pickNewDestination(const GameSnapshot& snap, bool recovery) {
        const auto boundsOpt = getMapBounds();
        if (!boundsOpt.has_value()) {
            hasDestination_ = false;
            hasAnchor_ = false;
            currentZoneId_ = -1;
            return false;
        }

        const MapBounds& bounds = *boundsOpt;
        const auto zones = buildRoamZones(bounds);
        if (zones.empty()) {
            hasDestination_ = false;
            hasAnchor_ = false;
            currentZoneId_ = -1;
            return false;
        }

        std::vector<int32_t> triedZoneIds;
        for (std::size_t attempt = 0; attempt < zones.size(); ++attempt) {
            auto zoneOpt = chooseZone(snap, zones, recovery);
            if (!zoneOpt.has_value()) {
                break;
            }

            const RoamZone& zone = *zoneOpt;
            if (std::find(triedZoneIds.begin(), triedZoneIds.end(), zone.id) != triedZoneIds.end()) {
                ignoredZones_[zone.id].ignoreUntilMs = snap.timestampMs + 5000;
                continue;
            }
            triedZoneIds.push_back(zone.id);

            auto destinationOpt = chooseDestinationInZone(zone, snap, bounds, recovery);
            if (!destinationOpt.has_value()) {
                ignoredZones_[zone.id].ignoreUntilMs = snap.timestampMs + 15000;
                continue;
            }

            currentZoneId_ = zone.id;
            anchor_ = zone.center();
            destination_ = *destinationOpt;
            hasAnchor_ = true;
            hasDestination_ = true;
            dwellUntilMs_ = 0;
            lastMoveTime_ = 0;
            lastDistanceToDestination_ = Position(snap.hero.x, snap.hero.y).distanceTo(destination_);
            lastProgressTime_ = snap.timestampMs;
            lastHeroPos_ = Position(snap.hero.x, snap.hero.y);

            std::cout << "[Roaming] New destination: (" << destination_.x
                      << ", " << destination_.y << ") zone=" << currentZoneId_ << "\n";
            return true;
        }

        if (auto fallback = pickEmergencyDestination(snap, bounds); fallback.has_value()) {
            currentZoneId_ = -1;
            anchor_ = *fallback;
            destination_ = *fallback;
            hasAnchor_ = true;
            hasDestination_ = true;
            dwellUntilMs_ = 0;
            lastMoveTime_ = 0;
            lastDistanceToDestination_ = Position(snap.hero.x, snap.hero.y).distanceTo(destination_);
            lastProgressTime_ = snap.timestampMs;
            lastHeroPos_ = Position(snap.hero.x, snap.hero.y);

            std::cout << "[Roaming] Emergency destination: (" << destination_.x
                      << ", " << destination_.y << ")\n";
            return true;
        }

        hasDestination_ = false;
        hasAnchor_ = false;
        currentZoneId_ = -1;
        return false;
    }
};

} // namespace dynamo
