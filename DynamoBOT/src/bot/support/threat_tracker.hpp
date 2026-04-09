#pragma once

/**
 * @file threat_tracker.hpp
 * @brief Tracks enemy threats and dangerous situations
 *
 * Monitors enemy players, attackers, and admins.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace dynamo {

namespace detail {

inline bool isKnownAdminName(std::string_view username) {
    return username == "Zaczarowana" ||
           username == "Dayn" ||
           username == "Curunir" ||
           username == "Vicc" ||
           username == "Serendipity" ||
           username == "Kub" ||
           username == "B4ron" ||
           username == "Exynos" ||
           username == "Gaia";
}

} // namespace detail

struct EnemyObservation {
    int32_t playerId{0};
    std::string username;
    double x{0.0};
    double y{0.0};
    double targetX{0.0};
    double targetY{0.0};
    bool isMoving{false};
    int32_t droneCount{0};
    bool isAttackingUs{false};
    int64_t timestampMs{0};
};

/**
 * @brief Information about a tracked enemy
 */
struct EnemyInfo {
    int32_t playerId{0};
    std::string username;
    int64_t firstSeenTime{0};
    int64_t lastSeenTime{0};
    int64_t lastAggressionTime{0};
    int32_t sightings{0};
    int32_t droneCount{0};
    bool isAttackingUs{false};
    bool isAdmin{false};
    bool isMoving{false};
    double lastX{0.0}, lastY{0.0};
    double lastTargetX{0.0}, lastTargetY{0.0};

    [[nodiscard]] double distanceTo(double heroX, double heroY) const {
        const double dx = lastX - heroX;
        const double dy = lastY - heroY;
        return std::sqrt(dx * dx + dy * dy);
    }
};

/**
 * @brief Threat level assessment
 */
enum class ThreatLevel {
    None,           // No threats
    Low,            // Enemy player nearby but not attacking
    Medium,         // Being attacked or dangerous approach
    High,           // Low HP + being attacked / concentrated threat
    Critical        // Must flee immediately
};

struct ThreatSummary {
    ThreatLevel level{ThreatLevel::None};
    int32_t visibleEnemies{0};
    int32_t closeEnemies{0};
    int32_t attackers{0};
    bool beingAttacked{false};
    bool adminNearby{false};
    bool hostileApproachDetected{false};
    int32_t primaryThreatId{0};
    double primaryThreatDistance{0.0};
    double nearestEnemyDistance{0.0};
    int64_t lastAttackTime{0};
};

/**
 * @brief Tracks and assesses threats in the game world
 */
class ThreatTracker {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit ThreatTracker(int32_t adminDroneThreshold = 8)
        : adminDroneThreshold_(adminDroneThreshold) {}

    void updateEnemy(const EnemyObservation& observation) {
        auto& info = enemies_[observation.playerId];
        if (info.playerId == 0) {
            info.playerId = observation.playerId;
            info.username = observation.username;
            info.firstSeenTime = observation.timestampMs;
        }

        info.lastSeenTime = observation.timestampMs;
        info.lastX = observation.x;
        info.lastY = observation.y;
        info.lastTargetX = observation.targetX;
        info.lastTargetY = observation.targetY;
        info.isMoving = observation.isMoving;
        info.droneCount = observation.droneCount;
        info.isAttackingUs = observation.isAttackingUs;
        info.isAdmin =
            observation.droneCount > adminDroneThreshold_ ||
            detail::isKnownAdminName(observation.username);
        info.sightings++;

        if (observation.isAttackingUs) {
            info.lastAggressionTime = observation.timestampMs;
            lastAttackTime_ = observation.timestampMs;
        }

        if (info.isAdmin) {
            lastAdminSeenTime_ = observation.timestampMs;
        }
    }

    void cleanupOldEnemies(int64_t currentTimeMs, int64_t timeoutMs = 15000) {
        for (auto it = enemies_.begin(); it != enemies_.end();) {
            if (currentTimeMs - it->second.lastSeenTime > timeoutMs) {
                it = enemies_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clear() {
        enemies_.clear();
        lastAttackTime_ = 0;
        lastAdminSeenTime_ = 0;
    }

    [[nodiscard]] ThreatSummary summarize(double heroHpPercent,
                                          double heroX,
                                          double heroY,
                                          int64_t currentTimeMs,
                                          int32_t enemyTimeoutMs = 15000,
                                          int32_t attackMemoryMs = 6000) const {
        ThreatSummary summary;
        summary.nearestEnemyDistance = std::numeric_limits<double>::max();
        double bestThreatScore = -std::numeric_limits<double>::max();

        for (const auto& [id, enemy] : enemies_) {
            if (!isFresh(enemy, currentTimeMs, enemyTimeoutMs)) {
                continue;
            }

            summary.visibleEnemies++;
            const double distance = enemy.distanceTo(heroX, heroY);
            summary.nearestEnemyDistance = std::min(summary.nearestEnemyDistance, distance);

            const bool attackerFresh = enemy.isAttackingUs &&
                currentTimeMs - enemy.lastSeenTime <= attackMemoryMs;
            const bool aggressionRecent =
                enemy.lastAggressionTime > 0 &&
                currentTimeMs - enemy.lastAggressionTime <= attackMemoryMs;
            const bool closeEnemy = distance <= CLOSE_THREAT_RANGE;
            const bool hostileApproach = isApproachingHero(enemy, heroX, heroY, distance);

            if (closeEnemy) {
                summary.closeEnemies++;
            }
            if (attackerFresh) {
                summary.attackers++;
                summary.beingAttacked = true;
            }
            if (enemy.isAdmin) {
                summary.adminNearby = true;
            }
            if (hostileApproach) {
                summary.hostileApproachDetected = true;
            }

            const double score = computeThreatScore(enemy, distance, attackerFresh, aggressionRecent, hostileApproach);
            if (score > bestThreatScore) {
                bestThreatScore = score;
                summary.primaryThreatId = enemy.playerId;
                summary.primaryThreatDistance = distance;
            }
        }

        summary.lastAttackTime = lastAttackTime_;
        if (summary.nearestEnemyDistance == std::numeric_limits<double>::max()) {
            summary.nearestEnemyDistance = 0.0;
        }

        summary.level = classifyThreat(heroHpPercent, summary);
        return summary;
    }

    [[nodiscard]] ThreatLevel assessThreat(double heroHpPercent,
                                           double heroX,
                                           double heroY,
                                           int64_t currentTimeMs,
                                           int32_t enemyTimeoutMs = 15000,
                                           int32_t attackMemoryMs = 6000) const {
        return summarize(
            heroHpPercent,
            heroX,
            heroY,
            currentTimeMs,
            enemyTimeoutMs,
            attackMemoryMs
        ).level;
    }

    [[nodiscard]] bool isBeingAttacked(int64_t currentTimeMs, int32_t recentMs = 5000) const {
        for (const auto& [id, enemy] : enemies_) {
            if (enemy.isAttackingUs && currentTimeMs - enemy.lastSeenTime < recentMs) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool adminSeenRecently(int64_t currentTimeMs, int32_t recentMs = 300000) const {
        return lastAdminSeenTime_ > 0 &&
               currentTimeMs - lastAdminSeenTime_ < recentMs;
    }

    [[nodiscard]] const EnemyInfo* getNearestEnemy(double heroX,
                                                   double heroY,
                                                   int64_t currentTimeMs,
                                                   int32_t timeoutMs = 15000) const {
        const EnemyInfo* nearest = nullptr;
        double minDist = std::numeric_limits<double>::max();

        for (const auto& [id, enemy] : enemies_) {
            if (!isFresh(enemy, currentTimeMs, timeoutMs)) {
                continue;
            }

            const double dist = enemy.distanceTo(heroX, heroY);
            if (dist < minDist) {
                minDist = dist;
                nearest = &enemy;
            }
        }

        return nearest;
    }

    [[nodiscard]] const EnemyInfo* getAttacker(int64_t currentTimeMs,
                                               int32_t recentMs = 5000) const {
        const EnemyInfo* attacker = nullptr;
        int64_t freshestAttack = std::numeric_limits<int64_t>::min();

        for (const auto& [id, enemy] : enemies_) {
            if (!enemy.isAttackingUs || currentTimeMs - enemy.lastSeenTime >= recentMs) {
                continue;
            }

            if (enemy.lastSeenTime > freshestAttack) {
                freshestAttack = enemy.lastSeenTime;
                attacker = &enemy;
            }
        }

        return attacker;
    }

    [[nodiscard]] const EnemyInfo* getPrimaryThreat(double heroX,
                                                    double heroY,
                                                    int64_t currentTimeMs,
                                                    int32_t enemyTimeoutMs = 15000,
                                                    int32_t attackMemoryMs = 6000) const {
        const EnemyInfo* primary = nullptr;
        double bestThreatScore = -std::numeric_limits<double>::max();

        for (const auto& [id, enemy] : enemies_) {
            if (!isFresh(enemy, currentTimeMs, enemyTimeoutMs)) {
                continue;
            }

            const double distance = enemy.distanceTo(heroX, heroY);
            const bool attackerFresh =
                enemy.isAttackingUs && currentTimeMs - enemy.lastSeenTime <= attackMemoryMs;
            const bool aggressionRecent =
                enemy.lastAggressionTime > 0 &&
                currentTimeMs - enemy.lastAggressionTime <= attackMemoryMs;
            const bool hostileApproach = isApproachingHero(enemy, heroX, heroY, distance);

            const double score = computeThreatScore(
                enemy,
                distance,
                attackerFresh,
                aggressionRecent,
                hostileApproach
            );

            if (score > bestThreatScore) {
                bestThreatScore = score;
                primary = &enemy;
            }
        }

        return primary;
    }

    [[nodiscard]] std::size_t enemyCount() const noexcept {
        return enemies_.size();
    }

    [[nodiscard]] std::optional<EnemyInfo> getFreshEnemy(int32_t playerId,
                                                         int64_t currentTimeMs,
                                                         int32_t timeoutMs = 15000) const {
        const auto it = enemies_.find(playerId);
        if (it == enemies_.end() || !isFresh(it->second, currentTimeMs, timeoutMs)) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::vector<EnemyInfo> freshEnemies(int64_t currentTimeMs,
                                                      int32_t timeoutMs = 15000) const {
        std::vector<EnemyInfo> result;
        result.reserve(enemies_.size());
        for (const auto& [_, enemy] : enemies_) {
            if (isFresh(enemy, currentTimeMs, timeoutMs)) {
                result.push_back(enemy);
            }
        }
        return result;
    }

private:
    static constexpr double CLOSE_THREAT_RANGE = 2200.0;
    static constexpr double DANGER_THREAT_RANGE = 1200.0;
    static constexpr double APPROACH_DISTANCE_BONUS_RANGE = 3200.0;

    std::unordered_map<int32_t, EnemyInfo> enemies_;
    int32_t adminDroneThreshold_{8};
    int64_t lastAttackTime_{0};
    int64_t lastAdminSeenTime_{0};

    [[nodiscard]] static bool isFresh(const EnemyInfo& enemy,
                                      int64_t currentTimeMs,
                                      int32_t timeoutMs) {
        return currentTimeMs - enemy.lastSeenTime <= timeoutMs;
    }

    [[nodiscard]] static bool isApproachingHero(const EnemyInfo& enemy,
                                                double heroX,
                                                double heroY,
                                                double currentDistance) {
        if (!enemy.isMoving) {
            return false;
        }

        const double nextDx = enemy.lastTargetX - heroX;
        const double nextDy = enemy.lastTargetY - heroY;
        const double nextDistance = std::sqrt(nextDx * nextDx + nextDy * nextDy);

        return nextDistance + 180.0 < currentDistance &&
               currentDistance <= APPROACH_DISTANCE_BONUS_RANGE;
    }

    [[nodiscard]] static double computeThreatScore(const EnemyInfo& enemy,
                                                   double distance,
                                                   bool attackerFresh,
                                                   bool aggressionRecent,
                                                   bool hostileApproach) {
        double score = 0.0;

        if (enemy.isAdmin) {
            score += 10000.0;
        }
        if (attackerFresh) {
            score += 6000.0;
        } else if (aggressionRecent) {
            score += 2500.0;
        }
        if (hostileApproach) {
            score += 900.0;
        }
        if (distance <= DANGER_THREAT_RANGE) {
            score += 1500.0;
        } else if (distance <= CLOSE_THREAT_RANGE) {
            score += 800.0;
        }

        score += std::clamp(3600.0 - distance, 0.0, 3600.0);
        return score;
    }

    [[nodiscard]] static ThreatLevel classifyThreat(double heroHpPercent,
                                                    const ThreatSummary& summary) {
        if (summary.visibleEnemies <= 0 && !summary.adminNearby) {
            return ThreatLevel::None;
        }

        if (heroHpPercent < 10.0 &&
            (summary.beingAttacked || summary.closeEnemies > 0 || summary.hostileApproachDetected)) {
            return ThreatLevel::Critical;
        }

        if (summary.adminNearby) {
            return summary.primaryThreatDistance <= CLOSE_THREAT_RANGE
                ? ThreatLevel::High
                : ThreatLevel::Medium;
        }

        if (summary.beingAttacked &&
            (heroHpPercent < 35.0 || summary.closeEnemies > 1 || summary.primaryThreatDistance <= DANGER_THREAT_RANGE)) {
            return ThreatLevel::High;
        }

        if (summary.beingAttacked) {
            return ThreatLevel::Medium;
        }

        if (summary.hostileApproachDetected &&
            (summary.primaryThreatDistance <= CLOSE_THREAT_RANGE || summary.closeEnemies > 0)) {
            return ThreatLevel::Medium;
        }

        if (summary.closeEnemies > 0 || summary.primaryThreatDistance <= CLOSE_THREAT_RANGE) {
            return ThreatLevel::Low;
        }

        return ThreatLevel::None;
    }
};

} // namespace dynamo
