#pragma once

/**
 * @file navigation.hpp
 * @brief Navigation and movement utilities
 * 
 * Handles pathfinding, flee routes, portal detection.
 */

#include <algorithm>
#include <cmath>
#include <random>
#include <optional>
#include <vector>
#include <cstdint>

namespace dynamo {

// Forward declaration
struct PortalInfo;
struct EntitiesSnapshot;

/**
 * @brief 2D position helper
 */
struct Position {
    double x{0};
    double y{0};
    
    Position() = default;
    Position(double x_, double y_) : x(x_), y(y_) {}
    
    [[nodiscard]] double distanceTo(const Position& other) const {
        double dx = other.x - x;
        double dy = other.y - y;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    [[nodiscard]] double squaredDistanceTo(const Position& other) const {
        double dx = other.x - x;
        double dy = other.y - y;
        return dx * dx + dy * dy;
    }
    
    [[nodiscard]] Position interpolate(const Position& target, double factor) const {
        return Position(
            x + (target.x - x) * factor,
            y + (target.y - y) * factor
        );
    }
    
    [[nodiscard]] Position offset(double dx, double dy) const {
        return Position(x + dx, y + dy);
    }
    
    bool operator==(const Position& other) const {
        return x == other.x && y == other.y;
    }
};

/**
 * @brief Map boundaries
 */
struct MapBounds {
    double minX{0};
    double minY{0};
    double maxX{21000};
    double maxY{13400};
    
    [[nodiscard]] bool contains(const Position& pos, double margin = 0) const {
        return pos.x >= minX + margin && pos.x <= maxX - margin &&
               pos.y >= minY + margin && pos.y <= maxY - margin;
    }
    
    [[nodiscard]] Position clamp(const Position& pos, double margin = 0) const {
        return Position(
            std::clamp(pos.x, minX + margin, maxX - margin),
            std::clamp(pos.y, minY + margin, maxY - margin)
        );
    }
    
    [[nodiscard]] Position center() const {
        return Position((minX + maxX) / 2, (minY + maxY) / 2);
    }
};

/**
 * @brief Navigation utilities for bot movement
 */
class Navigation {
public:
    Navigation() {
        std::random_device rd;
        rng_.seed(rd());
    }
    
    /**
     * @brief Generate random position within map bounds
     */
    [[nodiscard]] Position randomPosition(const MapBounds& bounds, double margin = 500) const {
        std::uniform_real_distribution<double> distX(bounds.minX + margin, bounds.maxX - margin);
        std::uniform_real_distribution<double> distY(bounds.minY + margin, bounds.maxY - margin);
        return Position(distX(rng_), distY(rng_));
    }
    
    /**
     * @brief Generate position to flee from a threat
     */
    [[nodiscard]] Position fleeFromPosition(const Position& hero, const Position& threat,
                                             const MapBounds& bounds, double fleeDistance = 3000) const {
        double dx = hero.x - threat.x;
        double dy = hero.y - threat.y;
        double dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist < 0.001) {
            // Threat is at our position, pick random direction
            std::uniform_real_distribution<double> angleDist(0, 2 * 3.14159265);
            double angle = angleDist(rng_);
            dx = std::cos(angle);
            dy = std::sin(angle);
        } else {
            // Normalize direction away from threat
            dx /= dist;
            dy /= dist;
        }
        
        // Add some randomness to avoid predictable patterns
        std::uniform_real_distribution<double> randAngle(-0.3, 0.3);
        double angleOffset = randAngle(rng_);
        double newDx = dx * std::cos(angleOffset) - dy * std::sin(angleOffset);
        double newDy = dx * std::sin(angleOffset) + dy * std::cos(angleOffset);
        
        Position target(hero.x + newDx * fleeDistance, hero.y + newDy * fleeDistance);
        return bounds.clamp(target, 300);
    }
    
    /**
     * @brief Find position around target at specified distance
     *
     * DarkBot-style: blends radial (maintain distance) with tangential (orbit)
     * components based on how far off the ideal radius we are.
     */
    [[nodiscard]] Position orbitPosition(const Position& hero, const Position& target,
                                          double orbitDistance, bool clockwise = true) const {
        double dx = hero.x - target.x;
        double dy = hero.y - target.y;
        double dist = std::sqrt(dx * dx + dy * dy);

        if (dist < 0.001) {
            return Position(target.x + orbitDistance, target.y);
        }

        // Normalize radial direction (away from target)
        double radX = dx / dist;
        double radY = dy / dist;

        // Tangential (perpendicular) direction for orbit
        double tanX = clockwise ? -radY : radY;
        double tanY = clockwise ? radX : -radX;

        // Blend: when at ideal distance, mostly tangential (orbit).
        // When too far/close, add radial correction.
        double distRatio = dist / orbitDistance;
        double radialWeight, tangentialWeight;

        if (distRatio > 1.3) {
            // Way too far - strong inward pull
            radialWeight = -0.8;  // negative = move toward target
            tangentialWeight = 0.2;
        } else if (distRatio > 1.1) {
            // Slightly too far - moderate inward + orbit
            double t = (distRatio - 1.1) / 0.2;
            radialWeight = -0.3 - t * 0.5;
            tangentialWeight = 0.7 - t * 0.5;
        } else if (distRatio < 0.7) {
            // Way too close - strong outward push
            radialWeight = 0.8;
            tangentialWeight = 0.2;
        } else if (distRatio < 0.9) {
            // Slightly too close - moderate outward + orbit
            double t = (0.9 - distRatio) / 0.2;
            radialWeight = 0.3 + t * 0.5;
            tangentialWeight = 0.7 - t * 0.5;
        } else {
            // Sweet spot - pure orbit with tiny correction
            radialWeight = (1.0 - distRatio) * 0.5; // subtle pull toward ideal
            tangentialWeight = 0.95;
        }

        double moveX = radX * radialWeight + tanX * tangentialWeight;
        double moveY = radY * radialWeight + tanY * tangentialWeight;

        // Normalize and project at orbit distance from target
        double moveDist = std::sqrt(moveX * moveX + moveY * moveY);
        if (moveDist < 0.001) moveDist = 1.0;
        moveX /= moveDist;
        moveY /= moveDist;

        return Position(target.x + moveX * orbitDistance, target.y + moveY * orbitDistance);
    }
    
    /**
     * @brief Find position near target for attack range
     */
    [[nodiscard]] Position approachPosition(const Position& hero, const Position& target,
                                             double desiredDistance) const {
        double dx = target.x - hero.x;
        double dy = target.y - hero.y;
        double dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist < desiredDistance) {
            // Already in range
            return hero;
        }
        
        // Move toward target but stop at desired distance
        double ratio = (dist - desiredDistance) / dist;
        return Position(hero.x + dx * ratio, hero.y + dy * ratio);
    }
    
    /**
     * @brief Add random jitter to position for anti-ban
     */
    [[nodiscard]] Position jitter(const Position& pos, double maxJitter = 50) const {
        std::uniform_real_distribution<double> dist(-maxJitter, maxJitter);
        return Position(pos.x + dist(rng_), pos.y + dist(rng_));
    }
    
    /**
     * @brief Predict target's future position based on movement vector
     * 
     * Uses target's current position and destination to extrapolate
     * where it will be in the near future. Essential for intercepting
     * moving NPCs instead of always chasing behind them.
     * 
     * @param targetPos Current position of target
     * @param targetDest Where target is moving to (targetX/targetY)
     * @param heroPos Our current position
     * @param heroSpeed Our movement speed
     * @return Predicted intercept position
     */
    [[nodiscard]] Position predictPosition(const Position& targetPos, const Position& targetDest,
                                            const Position& heroPos, double /*heroSpeed*/ = 300) const {
        // Calculate target's movement vector
        double moveVecX = targetDest.x - targetPos.x;
        double moveVecY = targetDest.y - targetPos.y;
        double moveDist = std::sqrt(moveVecX * moveVecX + moveVecY * moveVecY);
        
        if (moveDist < 10) {
            // Target not really moving
            return targetPos;
        }
        
        // Distance to target
        double distToTarget = heroPos.distanceTo(targetPos);
        
        // Prediction factor based on distance - predict further ahead when far away
        // At 500 units: factor ~0.125, at 2000 units: factor ~0.5
        double predictionFactor = std::min(1.0, (distToTarget - 400) / 3200.0);
        predictionFactor = std::max(0.0, predictionFactor);
        
        // Predicted position along movement vector
        double predictedX = targetPos.x + moveVecX * predictionFactor;
        double predictedY = targetPos.y + moveVecY * predictionFactor;
        
        return Position(predictedX, predictedY);
    }
    
    /**
     * @brief Calculate intercept position to catch a moving target
     * 
     * Similar to prediction but also accounts for randomization
     * to make movement look more natural.
     * 
     * @param targetPos Target's current position
     * @param targetDest Target's movement destination
     * @param heroPos Our position
     * @param followDistance How close we want to get
     * @return Position to move to for interception
     */
    [[nodiscard]] Position interceptPosition(const Position& targetPos, const Position& targetDest,
                                              const Position& heroPos, double followDistance = 500) const {
        Position predicted = predictPosition(targetPos, targetDest, heroPos);
        
        // Add natural randomization (like wupacket)
        double distToTarget = heroPos.distanceTo(targetPos);
        double maxRandom = std::min(300.0, distToTarget * 0.3);
        
        std::uniform_real_distribution<double> randDist(-maxRandom / 2, maxRandom / 2);
        predicted.x += randDist(rng_);
        predicted.y += randDist(rng_);
        
        // Now calculate approach to this predicted position
        return approachPosition(heroPos, predicted, followDistance);
    }
    
    /**
     * @brief Continuous orbit around target - DarkBot-style speed-aware circling
     *
     * Calculates next orbit waypoint with:
     * - Speed-aware angle step (accounts for hero + NPC speeds)
     * - Smooth distance correction spiral toward ideal radius
     * - Waypoint placed far enough ahead to keep movement flowing
     *
     * @param hero Current hero position
     * @param target Target to orbit around
     * @param orbitRadius Desired orbit distance
     * @param heroSpeed Hero movement speed (game units/s)
     * @param targetSpeed Target movement speed (game units/s, 0 for stationary)
     * @param clockwise Direction of orbit
     * @return Next position on orbit path
     */
    [[nodiscard]] Position continuousOrbit(const Position& hero, const Position& target,
                                            double orbitRadius, double heroSpeed = 300,
                                            double targetSpeed = 0,
                                            bool clockwise = true) const {
        double dx = hero.x - target.x;
        double dy = hero.y - target.y;
        double currentAngle = std::atan2(dy, dx);
        double currentDist = std::sqrt(dx * dx + dy * dy);

        if (currentDist < 0.001) {
            return Position(target.x + orbitRadius, target.y);
        }

        // DarkBot-style: subtract radial correction distance from speed budget
        // before computing tangential angle step. When far off-radius, nearly
        // all movement goes to radial correction, shrinking the orbit step.
        double radialCorrectionDist = std::abs(currentDist - orbitRadius);
        double linearBudget = heroSpeed * 0.625 + std::min(targetSpeed, 200.0) * 0.625;
        double tangentialBudget = std::max(linearBudget - radialCorrectionDist, 0.0);

        double angleDiff = tangentialBudget / std::max(orbitRadius, 100.0);
        angleDiff = std::clamp(angleDiff, 0.02, 0.35);

        double nextAngle = clockwise
            ? currentAngle - angleDiff
            : currentAngle + angleDiff;

        // The caller already passes the corrective radius (with distance
        // error baked in). Add chord compensation for the straight-line
        // movement that cuts inside the orbit arc.
        double halfAngle = std::min(angleDiff * 0.5, 0.25);
        double targetRadius = orbitRadius + orbitRadius * (1.0 - std::cos(halfAngle));

        double nextX = target.x + std::cos(nextAngle) * targetRadius;
        double nextY = target.y + std::sin(nextAngle) * targetRadius;

        return Position(nextX, nextY);
    }
    
    /**
     * @brief Calculate position for "kiting" - attack while moving away
     * 
     * For combat where you want to maintain distance while attacking.
     * Moves perpendicular to target with slight backward component.
     * 
     * @param hero Current position
     * @param target Target position
     * @param kiteDistance Desired distance from target
     * @return Next kite position
     */
    [[nodiscard]] Position kitePosition(const Position& hero, const Position& target,
                                         double kiteDistance = 550) const {
        double dx = hero.x - target.x;
        double dy = hero.y - target.y;
        double dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist < 0.001) {
            dx = 1; dy = 0; dist = 1;
        }
        
        // Normalize
        dx /= dist;
        dy /= dist;
        
        // Perpendicular direction (randomized)
        std::uniform_int_distribution<int> dirDist(0, 1);
        bool goRight = dirDist(rng_);
        double perpX = goRight ? -dy : dy;
        double perpY = goRight ? dx : -dx;
        
        // Mix: mostly perpendicular, slight backward
        double backFactor = 0.3;  // 30% backward, 70% sideways
        if (dist < kiteDistance * 0.8) {
            backFactor = 0.7;  // Too close - more backward
        } else if (dist > kiteDistance * 1.2) {
            backFactor = 0.0;  // Too far - pure sideways
        }
        
        double moveX = perpX * (1 - backFactor) + dx * backFactor;
        double moveY = perpY * (1 - backFactor) + dy * backFactor;
        
        // Normalize and scale
        double moveDist = std::sqrt(moveX * moveX + moveY * moveY);
        moveX /= moveDist;
        moveY /= moveDist;
        
        return Position(hero.x + moveX * 200, hero.y + moveY * 200);
    }

private:
    mutable std::mt19937 rng_;
};

} // namespace dynamo
