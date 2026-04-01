#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <string>
#include <chrono>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>

namespace dynamo {

inline bool isKnownWorldMapName(std::string_view mapName) {
    static const std::unordered_set<std::string_view> kKnownWorldMaps = {
        "R-1", "R-2", "R-3", "R-4", "R-5", "R-6", "R-7",
        "E-1", "E-2", "E-3", "E-4", "E-5", "E-6", "E-7",
        "U-1", "U-2", "U-3", "U-4", "U-5", "U-6", "U-7",
        "J-SO", "J-VO", "J-VS",
        "T-1", "G-1"
    };
    return kKnownWorldMaps.contains(mapName);
}

/**
 * @brief Extended ship info with bot-specific fields
 * 
 * War Universe terminology:
 * - Fraction (not Faction/Corporation) = player team/company
 * - BTC/PLT = currencies
 */
struct ShipInfo {
    int32_t id{0};
    std::string name;
    std::string clanTag;
    int32_t relation{0};
    int32_t clanRelation{0};
    int32_t fraction{0};        // War Universe "Fraction" (team ID)
    int32_t shipType{0};
    
    // Position (interpolated)
    float x{0}, y{0};
    float targetX{0}, targetY{0};
    bool isMoving{false};
    
    // Stats
    int32_t health{0}, maxHealth{0};
    int32_t shield{0}, maxShield{0};
    int32_t speed{0};
    int32_t cargo{0}, maxCargo{0};
    int32_t droneCount{0};
    
    // Combat state
    int32_t selectedTarget{0};  // Who this ship has selected
    bool isAttacking{false};
    bool inAttackRange{false};
    
    // Classification
    bool isNpc{false};
    bool isEnemy{false};        // Explicit hostile player signal
    bool isAlly{false};         // Explicit ally/clan signal
    bool isCloaked{false};
    bool isDestroyed{false};
    
    // Bot targeting data
    int32_t priority{0};        // Targeting priority (from config)
    
    // Observed velocity (computed from position deltas)
    float observedSpeedEma{0.0f};
    float observedVx{0.0f};
    float observedVy{0.0f};

    // Timestamps
    using TimePoint = std::chrono::steady_clock::time_point;
    TimePoint lastUpdateTime;
    TimePoint lastCoordUpdateTime;

    void updateObservedVelocity(float newX, float newY, TimePoint now) {
        auto elapsed = std::chrono::duration<float>(now - lastCoordUpdateTime).count();
        if (elapsed > 0.02f && elapsed < 3.0f) {
            float dx = newX - x;
            float dy = newY - y;
            float dist = std::sqrt(dx * dx + dy * dy);
            float instantSpeed = dist / elapsed;
            // Speed uses faster EMA (alpha=0.5) for quick response
            constexpr float speedAlpha = 0.5f;
            observedSpeedEma = observedSpeedEma * (1.0f - speedAlpha) + instantSpeed * speedAlpha;
            // Direction uses slower EMA (alpha=0.35) for smoothness
            if (dist > 1.0f) {
                constexpr float dirAlpha = 0.35f;
                observedVx = observedVx * (1.0f - dirAlpha) + (dx / elapsed) * dirAlpha;
                observedVy = observedVy * (1.0f - dirAlpha) + (dy / elapsed) * dirAlpha;
            }
        }
    }

    // Helpers
    float healthPercent() const {
        return maxHealth > 0 ? (static_cast<float>(health) / maxHealth) * 100.0f : 0.0f;
    }
    
    float shieldPercent() const {
        return maxShield > 0 ? (static_cast<float>(shield) / maxShield) * 100.0f : 0.0f;
    }
    
    float distanceTo(float ox, float oy) const {
        float dx = x - ox;
        float dy = y - oy;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    float distanceTo(const ShipInfo& other) const {
        return distanceTo(other.x, other.y);
    }
    
    // Update position via interpolation
    void interpolatePosition(float deltaSeconds) {
        if (!isMoving || speed <= 0) return;
        
        float dx = targetX - x;
        float dy = targetY - y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist < 1.0f) {
            x = targetX;
            y = targetY;
            isMoving = false;
            return;
        }
        
        float maxMove = speed * deltaSeconds;
        if (maxMove >= dist) {
            x = targetX;
            y = targetY;
            isMoving = false;
        } else {
            float ratio = maxMove / dist;
            x += dx * ratio;
            y += dy * ratio;
        }
    }
};

/**
 * @brief Extended collectable info
 */
struct BoxInfo {
    int32_t id{0};
    int32_t type{0};        // 0=bonus, 1=cargo, 2=energy, 3=green
    int32_t subtype{0};
    float x{0}, y{0};
    bool existsOnMap{true};
    
    // Bot targeting data
    int32_t priority{0};
    
    float distanceTo(float ox, float oy) const {
        float dx = x - ox;
        float dy = y - oy;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    bool isBonusBox() const { return type == 0; }
    bool isCargoBox() const { return type == 1; }
    bool isEnergyBox() const { return type == 2; }
    bool isGreenBox() const { return type == 3; }
    bool isBootyBox() const { return isGreenBox(); }
};

/**
 * @brief Portal/teleporter info
 */
struct PortalInfo {
    int32_t id{0};
    float x{0}, y{0};
    int32_t targetMapId{0};
    std::string targetMapName;
    std::string type;
    bool active{true};

    float distanceTo(float ox, float oy) const {
        float dx = x - ox;
        float dy = y - oy;
        return std::sqrt(dx * dx + dy * dy);
    }

    bool isWorldPortal() const {
        if (!active) {
            return false;
        }

        if (type == "MISSION_TELEPORT") {
            return false;
        }

        if (!targetMapName.empty()) {
            return isKnownWorldMapName(targetMapName);
        }

        return type.empty() ||
               type == "portal" ||
               type == "teleporter" ||
               type == "TELEPORT" ||
               type == "TELEPORTER" ||
               type == "NORMAL_TELEPORT";
    }
};

/**
 * @brief Station info (space station, trade station, etc.)
 */
struct StationInfo {
    int32_t id{0};
    float x{0}, y{0};
    std::string type;  // SPACE_STATION, TRADE_STATION, QUEST_STATION, etc.
    int32_t subtype{0};
    std::string name;
    
    float distanceTo(float ox, float oy) const {
        float dx = x - ox;
        float dy = y - oy;
        return std::sqrt(dx * dx + dy * dy);
    }
};

/**
 * @brief Convoy event target info
 *
 * Derived from convoy game events. npcShipId points to a regular ship entity
 * that should be highlighted on the map.
 */
struct ConvoyInfo {
    int32_t sourceShipId{0};
    int32_t npcShipId{0};
    int32_t state{0};
    int32_t phase{0};
    using TimePoint = std::chrono::steady_clock::time_point;
    TimePoint lastUpdateTime;
};

/**
 * @brief Snapshot of entities for thread-safe access
 * 
 * This is a complete copy of entity state at a point in time.
 * Bot logic can safely iterate without holding locks.
 */
struct EntitiesSnapshot {
    std::vector<ShipInfo> ships;
    std::vector<ShipInfo> npcs;
    std::vector<ShipInfo> enemies;
    std::vector<ShipInfo> allies;
    std::vector<BoxInfo> boxes;
    std::vector<PortalInfo> portals;
    std::vector<StationInfo> stations;
    std::vector<ConvoyInfo> convoys;
    
    // Convenience methods
    std::optional<ShipInfo> findShip(int32_t id) const {
        for (const auto& ship : ships) {
            if (ship.id == id) return ship;
        }
        return std::nullopt;
    }
    
    std::optional<BoxInfo> findBox(int32_t id) const {
        for (const auto& box : boxes) {
            if (box.id == id) return box;
        }
        return std::nullopt;
    }
    
    std::optional<ShipInfo> nearestNpc(float fromX, float fromY) const {
        std::optional<ShipInfo> nearest;
        float minDist = std::numeric_limits<float>::max();
        
        for (const auto& npc : npcs) {
            if (npc.isDestroyed) continue;
            float dist = npc.distanceTo(fromX, fromY);
            if (dist < minDist) {
                minDist = dist;
                nearest = npc;
            }
        }
        return nearest;
    }
    
    std::optional<ShipInfo> nearestEnemy(float fromX, float fromY) const {
        std::optional<ShipInfo> nearest;
        float minDist = std::numeric_limits<float>::max();
        
        for (const auto& enemy : enemies) {
            if (enemy.isDestroyed) continue;
            float dist = enemy.distanceTo(fromX, fromY);
            if (dist < minDist) {
                minDist = dist;
                nearest = enemy;
            }
        }
        return nearest;
    }
    
    std::optional<BoxInfo> nearestBox(float fromX, float fromY, 
                                       const std::unordered_set<int32_t>& targetTypes = {}) const {
        std::optional<BoxInfo> nearest;
        float minDist = std::numeric_limits<float>::max();
        
        for (const auto& box : boxes) {
            if (!box.existsOnMap) continue;
            if (!targetTypes.empty() && targetTypes.find(box.type) == targetTypes.end()) {
                continue;
            }
            float dist = box.distanceTo(fromX, fromY);
            if (dist < minDist) {
                minDist = dist;
                nearest = box;
            }
        }
        return nearest;
    }
    
    std::optional<PortalInfo> nearestPortal(float fromX, float fromY) const {
        std::optional<PortalInfo> nearest;
        float minDist = std::numeric_limits<float>::max();
        
        for (const auto& portal : portals) {
            if (!portal.isWorldPortal()) continue;
            float dist = portal.distanceTo(fromX, fromY);
            if (dist < minDist) {
                minDist = dist;
                nearest = portal;
            }
        }
        return nearest;
    }
    
    // Find best NPC target using priority scoring
    std::optional<ShipInfo> bestNpcTarget(float fromX, float fromY,
                                          const std::vector<std::string>& targetNames = {}) const {
        std::optional<ShipInfo> best;
        float bestScore = -std::numeric_limits<float>::max();
        
        for (const auto& npc : npcs) {
            if (npc.isDestroyed) continue;
            
            // Check if in target list
            bool inTargetList = targetNames.empty();
            int32_t priority = 1;
            for (size_t i = 0; i < targetNames.size(); ++i) {
                if (npc.name.find(targetNames[i]) != std::string::npos) {
                    inTargetList = true;
                    priority = static_cast<int32_t>(targetNames.size() - i); // Higher priority for earlier entries
                    break;
                }
            }
            if (!inTargetList) continue;
            
            // Skip if another ship is attacking this NPC
            bool beingAttacked = false;
            for (const auto& ship : ships) {
                if (ship.id != npc.id && ship.selectedTarget == npc.id && ship.isAttacking) {
                    beingAttacked = true;
                    break;
                }
            }
            if (beingAttacked) continue;
            
            float dist = npc.distanceTo(fromX, fromY);
            float distScore = 1.0f - std::min(dist / 2000.0f, 1.0f);
            float score = priority * 1000.0f + distScore * 100.0f;
            
            if (score > bestScore) {
                bestScore = score;
                best = npc;
            }
        }
        return best;
    }
    
    // Find best box target using priority scoring
    std::optional<BoxInfo> bestBoxTarget(float fromX, float fromY,
                                          const std::unordered_set<int32_t>& targetTypes = {},
                                          int32_t bootyKeys = 0,
                                          int32_t currentCargo = 0,
                                          int32_t maxCargo = 100) const {
        std::optional<BoxInfo> best;
        float bestScore = -std::numeric_limits<float>::max();
        
        bool cargoFull = currentCargo >= maxCargo - 5;
        
        for (const auto& box : boxes) {
            if (!box.existsOnMap) continue;
            
            // Check type filter
            if (!targetTypes.empty()) {
                auto it = targetTypes.find(box.type);
                if (it == targetTypes.end()) continue;
            }
            
            // Skip green/booty boxes if no keys
            if (box.isBootyBox() && bootyKeys <= 0) continue;
            
            // Skip cargo boxes if cargo is effectively full.
            if (box.isCargoBox() && cargoFull) continue;
            
            float dist = box.distanceTo(fromX, fromY);
            float distScore = 1.0f - std::min(dist / 2000.0f, 1.0f);
            float score = box.priority * 1000.0f + distScore * 100.0f;
            
            if (score > bestScore) {
                bestScore = score;
                best = box;
            }
        }
        return best;
    }
};

/**
 * @brief Entity manager with thread-safe access
 */
class Entities {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    
    Entities() = default;
    
    //--------------------------------------------------------------------------
    // Ship management
    //--------------------------------------------------------------------------
    
    void updateShip(int32_t id, const std::function<void(ShipInfo&)>& updater) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& ship = ships_[id];
        if (ship.id == 0) ship.id = id;
        updater(ship);
        ship.lastUpdateTime = std::chrono::steady_clock::now();
    }
    
    void removeShip(int32_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        ships_.erase(id);
        convoys_.erase(id);
    }
    
    std::optional<ShipInfo> getShip(int32_t id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ships_.find(id);
        if (it != ships_.end()) return it->second;
        return std::nullopt;
    }
    
    //--------------------------------------------------------------------------
    // Box management
    //--------------------------------------------------------------------------
    
    void updateBox(int32_t id, const std::function<void(BoxInfo&)>& updater) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& box = boxes_[id];
        if (box.id == 0) box.id = id;
        updater(box);
    }
    
    void removeBox(int32_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        boxes_.erase(id);
    }
    
    std::optional<BoxInfo> getBox(int32_t id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = boxes_.find(id);
        if (it != boxes_.end()) return it->second;
        return std::nullopt;
    }
    
    //--------------------------------------------------------------------------
    // Portal management
    //--------------------------------------------------------------------------
    
    void updatePortal(int32_t id, const PortalInfo& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        portals_[id] = info;
    }
    
    void clearPortals() {
        std::lock_guard<std::mutex> lock(mutex_);
        portals_.clear();
    }
    
    //--------------------------------------------------------------------------
    // Station management
    //--------------------------------------------------------------------------
    
    void updateStation(int32_t id, const StationInfo& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        stations_[id] = info;
    }
    
    void clearStations() {
        std::lock_guard<std::mutex> lock(mutex_);
        stations_.clear();
    }
    
    //--------------------------------------------------------------------------
    // Convoy management
    //--------------------------------------------------------------------------
    
    void updateConvoy(int32_t npcShipId, const ConvoyInfo& info) {
        std::lock_guard<std::mutex> lock(mutex_);
        convoys_[npcShipId] = info;
    }
    
    void removeConvoy(int32_t npcShipId) {
        std::lock_guard<std::mutex> lock(mutex_);
        convoys_.erase(npcShipId);
    }
    
    void clearConvoys() {
        std::lock_guard<std::mutex> lock(mutex_);
        convoys_.clear();
    }
    
    //--------------------------------------------------------------------------
    // Bulk operations
    //--------------------------------------------------------------------------
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        ships_.clear();
        boxes_.clear();
        portals_.clear();
        stations_.clear();
        convoys_.clear();
    }
    
    /**
     * @brief Interpolate all ship positions
     * @param deltaSeconds Time since last update
     */
    void interpolatePositions(float deltaSeconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, ship] : ships_) {
            ship.interpolatePosition(deltaSeconds);
        }
    }
    
    /**
     * @brief Remove entities that haven't been updated recently
     * @param maxAgeMs Maximum age in milliseconds
     */
    void pruneStale(int maxAgeMs = 3000) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        
        for (auto it = ships_.begin(); it != ships_.end(); ) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.lastUpdateTime).count();
            if (age > maxAgeMs) {
                it = ships_.erase(it);
            } else {
                ++it;
            }
        }
        
        // Remove boxes that no longer exist
        for (auto it = boxes_.begin(); it != boxes_.end(); ) {
            if (!it->second.existsOnMap) {
                it = boxes_.erase(it);
            } else {
                ++it;
            }
        }
        
        // Convoy entries are tied to ship entities and should expire quickly
        for (auto it = convoys_.begin(); it != convoys_.end(); ) {
            const bool missingShip = ships_.find(it->first) == ships_.end();
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.lastUpdateTime).count();
            if (missingShip || age > 60000) {
                it = convoys_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    /**
     * @brief Get snapshot of all entities
     * @param playerId Player ID to exclude from enemies
     * @param playerFraction Player fraction for player classification
     * @param playerClanTag Player clan tag for explicit clan-mate detection
     * @return Copy of all entities
     */
    EntitiesSnapshot snapshot(int32_t playerId = 0,
                              int32_t playerFraction = 0,
                              std::string playerClanTag = {}) const {
        std::lock_guard<std::mutex> lock(mutex_);
        EntitiesSnapshot snap;
        
        for (const auto& [id, ship] : ships_) {
            snap.ships.push_back(ship);
            
            if (ship.isNpc) {
                snap.npcs.push_back(ship);
            } else if (id != playerId) {
                // Server-authoritative flags take priority over heuristic classification.
                if (ship.isEnemy) {
                    snap.enemies.push_back(ship);
                } else if (ship.isAlly) {
                    snap.allies.push_back(ship);
                } else if (!playerClanTag.empty() &&
                           !ship.clanTag.empty() &&
                           ship.clanTag == playerClanTag) {
                    snap.allies.push_back(ship);
                } else if (playerFraction > 0 && ship.fraction == playerFraction) {
                    snap.allies.push_back(ship);
                } else if (playerFraction > 0 && ship.fraction != playerFraction && ship.fraction > 0) {
                    snap.enemies.push_back(ship);
                } else if (playerFraction <= 0) {
                    // Delay unresolved player classification until we know our own fraction.
                    continue;
                } else {
                    // Unknown relation and no valid fraction data — treat as enemy
                    snap.enemies.push_back(ship);
                }
            }
        }
        
        for (const auto& [id, box] : boxes_) {
            if (box.existsOnMap) {
                snap.boxes.push_back(box);
            }
        }
        
        for (const auto& [id, portal] : portals_) {
            snap.portals.push_back(portal);
        }
        
        for (const auto& [id, station] : stations_) {
            snap.stations.push_back(station);
        }
        
        for (const auto& [id, convoy] : convoys_) {
            snap.convoys.push_back(convoy);
        }
        
        return snap;
    }
    
private:
    mutable std::mutex mutex_;
    std::unordered_map<int32_t, ShipInfo> ships_;
    std::unordered_map<int32_t, BoxInfo> boxes_;
    std::unordered_map<int32_t, PortalInfo> portals_;
    std::unordered_map<int32_t, StationInfo> stations_;
    std::unordered_map<int32_t, ConvoyInfo> convoys_;
};

} // namespace dynamo
