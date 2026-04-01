#pragma once

/**
 * @file movement_controller.hpp
 * @brief Centralized movement backend for bot modules
 *
 * Humanizes bot movement by converting raw destinations into short,
 * bounded waypoint routes with lightweight replanning.
 */

#include "bot/core/module.hpp"
#include "bot/support/navigation.hpp"
#include "game/game_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace dynamo {

enum class MoveIntent {
    Precision,
    Travel,
    Collect,
    Pursuit,
    Orbit,
    Escape,
    Roam,
    Antiban
};

struct MoveProfile {
    double waypointRadius{120.0};
    double finalArrivalRadius{90.0};
    double segmentLength{600.0};
    double maxDeviation{120.0};
    double mapMargin{180.0};
    double destinationChangeThreshold{150.0};
    double replanDistance{320.0};
    int64_t retryIntervalMs{650};
    int64_t maxRouteAgeMs{3500};
    bool exactFinalApproach{true};
};

class MovementController {
public:
    explicit MovementController(std::shared_ptr<GameEngine> engine)
        : engine_(std::move(engine))
        , rng_(std::random_device{}()) {}

    void reset() noexcept {
        route_ = RouteState{};
    }

    void release(std::string_view owner) noexcept {
        if (route_.owner == owner) {
            reset();
        }
    }

    bool move(std::string_view owner,
              const GameSnapshot& snap,
              const Position& requestedDestination,
              MoveIntent intent) {
        if (!engine_) {
            return false;
        }

        const MapBounds bounds = currentBounds();
        const MoveProfile profile = profileFor(intent, snap.hero.speed);
        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position destination = bounds.clamp(requestedDestination, profile.mapMargin);

        if (heroPos.distanceTo(destination) <= profile.finalArrivalRadius) {
            release(owner);
            return false;
        }

        if (shouldReplan(owner, snap, heroPos, destination, intent, profile)) {
            planRoute(owner, snap.timestampMs, heroPos, destination, bounds, intent, profile);
        } else {
            advanceRoute(heroPos);
        }

        if (route_.nextWaypointIndex >= route_.waypoints.size()) {
            reset();
            return false;
        }

        const auto& next = route_.waypoints[route_.nextWaypointIndex];
        const double moveThreshold = (intent == MoveIntent::Orbit) ? 25.0 : 80.0;
        const bool waypointChanged =
            route_.lastIssuedWaypoint.squaredDistanceTo(next.position) > (moveThreshold * moveThreshold);
        const bool retryExpired =
            snap.timestampMs - route_.lastIssuedAtMs >= profile.retryIntervalMs;

        if (!waypointChanged && !retryExpired) {
            return false;
        }

        engine_->moveTo(static_cast<float>(next.position.x), static_cast<float>(next.position.y));
        route_.lastIssuedWaypoint = next.position;
        route_.lastIssuedAtMs = snap.timestampMs;
        return true;
    }

private:
    struct Waypoint {
        Position position;
        double radius{120.0};
    };

    struct RouteState {
        std::string owner;
        MoveIntent intent{MoveIntent::Precision};
        Position finalDestination;
        Position routeStart;
        Position lastIssuedWaypoint;
        std::vector<Waypoint> waypoints;
        std::size_t nextWaypointIndex{0};
        int64_t plannedAtMs{0};
        int64_t lastIssuedAtMs{0};
    };

    std::shared_ptr<GameEngine> engine_;
    mutable std::mt19937 rng_;
    RouteState route_;

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

    [[nodiscard]] MoveProfile profileFor(MoveIntent intent, int32_t heroSpeed) const {
        MoveProfile profile;

        switch (intent) {
            case MoveIntent::Travel:
                profile.waypointRadius = 160.0;
                profile.finalArrivalRadius = 130.0;
                profile.segmentLength = 900.0;
                profile.maxDeviation = 180.0;
                profile.mapMargin = 240.0;
                profile.destinationChangeThreshold = 220.0;
                profile.replanDistance = 520.0;
                profile.retryIntervalMs = 700;
                profile.maxRouteAgeMs = 6000;
                profile.exactFinalApproach = true;
                break;
            case MoveIntent::Collect:
                profile.waypointRadius = 120.0;
                profile.finalArrivalRadius = 105.0;
                profile.segmentLength = 520.0;
                profile.maxDeviation = 85.0;
                profile.mapMargin = 160.0;
                profile.destinationChangeThreshold = 100.0;
                profile.replanDistance = 260.0;
                profile.retryIntervalMs = 500;
                profile.maxRouteAgeMs = 3000;
                profile.exactFinalApproach = true;
                break;
            case MoveIntent::Pursuit:
                profile.waypointRadius = 140.0;
                profile.finalArrivalRadius = 120.0;
                profile.segmentLength = 440.0;
                profile.maxDeviation = 110.0;
                profile.mapMargin = 170.0;
                profile.destinationChangeThreshold = 110.0;
                profile.replanDistance = 260.0;
                profile.retryIntervalMs = 420;
                profile.maxRouteAgeMs = 1800;
                profile.exactFinalApproach = false;
                break;
            case MoveIntent::Orbit:
                profile.waypointRadius = 55.0;
                profile.finalArrivalRadius = 45.0;
                profile.segmentLength = 200.0;
                profile.maxDeviation = 12.0;
                profile.mapMargin = 220.0;
                profile.destinationChangeThreshold = 55.0;
                profile.replanDistance = 100.0;
                profile.retryIntervalMs = 250;
                profile.maxRouteAgeMs = 800;
                profile.exactFinalApproach = true;
                break;
            case MoveIntent::Escape:
                profile.waypointRadius = 180.0;
                profile.finalArrivalRadius = 150.0;
                profile.segmentLength = 880.0;
                profile.maxDeviation = 55.0;
                profile.mapMargin = 280.0;
                profile.destinationChangeThreshold = 260.0;
                profile.replanDistance = 600.0;
                profile.retryIntervalMs = 360;
                profile.maxRouteAgeMs = 2800;
                profile.exactFinalApproach = true;
                break;
            case MoveIntent::Roam:
                profile.waypointRadius = 220.0;
                profile.finalArrivalRadius = 130.0;
                profile.segmentLength = 900.0;
                profile.maxDeviation = 220.0;
                profile.mapMargin = 320.0;
                profile.destinationChangeThreshold = 220.0;
                profile.replanDistance = 520.0;
                profile.retryIntervalMs = 260;
                profile.maxRouteAgeMs = 2600;
                profile.exactFinalApproach = true;
                break;
            case MoveIntent::Antiban:
                profile.waypointRadius = 120.0;
                profile.finalArrivalRadius = 95.0;
                profile.segmentLength = 260.0;
                profile.maxDeviation = 45.0;
                profile.mapMargin = 200.0;
                profile.destinationChangeThreshold = 80.0;
                profile.replanDistance = 200.0;
                profile.retryIntervalMs = 500;
                profile.maxRouteAgeMs = 1500;
                profile.exactFinalApproach = true;
                break;
            case MoveIntent::Precision:
            default:
                break;
        }

        if (heroSpeed > 0) {
            profile.segmentLength = std::clamp(
                profile.segmentLength + static_cast<double>(heroSpeed) * 0.3,
                240.0,
                1400.0
            );
        }

        return profile;
    }

    [[nodiscard]] bool shouldReplan(std::string_view owner,
                                    const GameSnapshot& snap,
                                    const Position& heroPos,
                                    const Position& destination,
                                    MoveIntent intent,
                                    const MoveProfile& profile) {
        if (route_.owner != owner) {
            return true;
        }
        if (route_.intent != intent) {
            return true;
        }
        if (route_.waypoints.empty() || route_.nextWaypointIndex >= route_.waypoints.size()) {
            return true;
        }
        if (route_.finalDestination.distanceTo(destination) > profile.destinationChangeThreshold) {
            return true;
        }
        if (snap.timestampMs - route_.plannedAtMs > profile.maxRouteAgeMs) {
            return true;
        }

        advanceRoute(heroPos);
        if (route_.nextWaypointIndex >= route_.waypoints.size()) {
            return true;
        }

        const Position segmentStart = route_.nextWaypointIndex == 0
            ? route_.routeStart
            : route_.waypoints[route_.nextWaypointIndex - 1].position;
        const Position segmentEnd = route_.waypoints[route_.nextWaypointIndex].position;

        return distanceToSegment(heroPos, segmentStart, segmentEnd) > profile.replanDistance;
    }

    void planRoute(std::string_view owner,
                   int64_t nowMs,
                   const Position& heroPos,
                   const Position& destination,
                   const MapBounds& bounds,
                   MoveIntent intent,
                   const MoveProfile& profile) {
        route_ = RouteState{};
        route_.owner = std::string(owner);
        route_.intent = intent;
        route_.finalDestination = destination;
        route_.routeStart = heroPos;
        route_.plannedAtMs = nowMs;
        route_.lastIssuedWaypoint = heroPos;
        route_.waypoints = buildRoute(heroPos, destination, bounds, profile);
        route_.nextWaypointIndex = 0;
    }

    [[nodiscard]] std::vector<Waypoint> buildRoute(const Position& start,
                                                   const Position& destination,
                                                   const MapBounds& bounds,
                                                   const MoveProfile& profile) {
        std::vector<Waypoint> route;
        const double totalDistance = start.distanceTo(destination);
        if (totalDistance <= 1.0) {
            return route;
        }

        const int segmentCount = std::max(
            1,
            static_cast<int>(std::ceil(totalDistance / std::max(1.0, profile.segmentLength)))
        );

        double dirX = destination.x - start.x;
        double dirY = destination.y - start.y;
        const double dirLength = std::sqrt(dirX * dirX + dirY * dirY);
        if (dirLength > 0.0) {
            dirX /= dirLength;
            dirY /= dirLength;
        } else {
            dirX = 1.0;
            dirY = 0.0;
        }

        const double perpX = -dirY;
        const double perpY = dirX;
        std::uniform_real_distribution<double> signDist(-1.0, 1.0);
        std::uniform_real_distribution<double> jitterDist(0.75, 1.15);
        std::uniform_real_distribution<double> tangentDist(-0.07, 0.07);

        const double signedDeviation = signDist(rng_) * std::min(
            profile.maxDeviation,
            std::max(0.0, totalDistance * 0.18)
        );

        route.reserve(static_cast<std::size_t>(segmentCount) + 1);

        for (int step = 1; step <= segmentCount; ++step) {
            const double t = static_cast<double>(step) / segmentCount;
            Position point = start.interpolate(destination, t);

            const bool isFinalStep = step == segmentCount;
            if (!(isFinalStep && profile.exactFinalApproach) && totalDistance > 260.0) {
                const double envelope = std::sin(t * std::numbers::pi_v<double>);
                const double lateralOffset = signedDeviation * envelope * jitterDist(rng_);
                const double forwardOffset = tangentDist(rng_) * totalDistance * envelope;

                point = point.offset(
                    perpX * lateralOffset + dirX * forwardOffset,
                    perpY * lateralOffset + dirY * forwardOffset
                );
            }

            point = bounds.clamp(point, profile.mapMargin);
            if (!route.empty() &&
                route.back().position.squaredDistanceTo(point) < (40.0 * 40.0)) {
                continue;
            }

            Waypoint waypoint;
            waypoint.position = point;
            waypoint.radius = isFinalStep && !profile.exactFinalApproach
                ? profile.finalArrivalRadius
                : profile.waypointRadius;
            route.push_back(waypoint);
        }

        if (profile.exactFinalApproach) {
            if (route.empty() ||
                route.back().position.distanceTo(destination) > profile.finalArrivalRadius * 0.5) {
                route.push_back(Waypoint{destination, profile.finalArrivalRadius});
            } else {
                route.back().position = destination;
                route.back().radius = profile.finalArrivalRadius;
            }
        }

        return route;
    }

    void advanceRoute(const Position& heroPos) {
        while (route_.nextWaypointIndex < route_.waypoints.size()) {
            const auto& waypoint = route_.waypoints[route_.nextWaypointIndex];
            if (heroPos.distanceTo(waypoint.position) > waypoint.radius) {
                break;
            }
            route_.nextWaypointIndex++;
        }
    }

    [[nodiscard]] static double distanceToSegment(const Position& point,
                                                  const Position& start,
                                                  const Position& end) {
        const double segX = end.x - start.x;
        const double segY = end.y - start.y;
        const double segLenSq = segX * segX + segY * segY;
        if (segLenSq <= 1e-6) {
            return point.distanceTo(start);
        }

        const double proj = ((point.x - start.x) * segX + (point.y - start.y) * segY) / segLenSq;
        const double clamped = std::clamp(proj, 0.0, 1.0);
        const Position projected(start.x + segX * clamped, start.y + segY * clamped);
        return point.distanceTo(projected);
    }
};

} // namespace dynamo
