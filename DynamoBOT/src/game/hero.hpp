#pragma once

#include <string>
#include <chrono>
#include <cmath>
#include <mutex>

namespace dynamo {

/**
 * @brief Hero (player) state - War Universe specific
 * 
 * Comprehensive player state including stats, resources, and combat info.
 * All naming follows War Universe conventions (not DarkOrbit).
 */
struct Hero {
    // Identity
    int32_t id{0};
    std::string name;
    std::string clanTag;
    int32_t fraction{0};        // War Universe "Fraction" (team ID)
    
    // Position
    float x{0}, y{0};
    float targetX{0}, targetY{0};
    bool isMoving{false};
    
    // Predicted position (for immediate feedback)
    float predictedX{0}, predictedY{0};
    bool hasPredictedPosition{false};
    
    // Stats
    int32_t health{0}, maxHealth{0};
    int32_t shield{0}, maxShield{0};
    int32_t speed{0};
    
    // Resources (War Universe names from UserInfoResponsePacket)
    int64_t btc{0};             // BTC - credits (param id 3)
    int64_t plt{0};             // PLT - premium currency (param id 4)
    int64_t experience{0};      // EXP (param id 5)
    int32_t honor{0};           // Honor (param id 6)
    int32_t level{0};           // Level (param id 7)
    int32_t bootyKeys{0};       // Keys for booty boxes (param id 43)
    
    // Cargo
    int32_t cargo{0};
    int32_t maxCargo{0};
    
    // Laser Ammo (War Universe types, param id 8)
    struct LaserAmmo {
        int32_t rlx1{0};     // RLX-1 (type 1) - basic
        int32_t glx2{0};     // GLX-2 (type 2) - medium
        int32_t blx3{0};     // BLX-3 (type 3) - strong
        int32_t wlx4{0};     // WLX-4 (type 4) - special (cannot buy)
        int32_t glx2as{0};   // GLX-2-AS (type 5) - anti-shield
        int32_t mrs6x{0};    // MRS-6X (type 6) - premium
        
        int32_t get(int type) const {
            switch (type) {
                case 1: return rlx1;
                case 2: return glx2;
                case 3: return blx3;
                case 4: return wlx4;
                case 5: return glx2as;
                case 6: return mrs6x;
                default: return 0;
            }
        }
        
        void set(int type, int32_t value) {
            switch (type) {
                case 1: rlx1 = value; break;
                case 2: glx2 = value; break;
                case 3: blx3 = value; break;
                case 4: wlx4 = value; break;
                case 5: glx2as = value; break;
                case 6: mrs6x = value; break;
            }
        }
    } lasers;
    
    // Rocket Ammo (War Universe types, param id 9)
    struct RocketAmmo {
        int32_t kep410{0};   // KEP-410 (type 1) - basic
        int32_t nc30{0};     // NC-30 (type 2) - medium
        int32_t tnc130{0};   // TNC-130 (type 3) - strong
        
        int32_t get(int type) const {
            switch (type) {
                case 1: return kep410;
                case 2: return nc30;
                case 3: return tnc130;
                default: return 0;
            }
        }
        
        void set(int type, int32_t value) {
            switch (type) {
                case 1: kep410 = value; break;
                case 2: nc30 = value; break;
                case 3: tnc130 = value; break;
            }
        }
    } rockets;
    
    // Energy/Fuel (War Universe types, param id 10)
    struct Energy {
        int32_t ee{0};       // EE (type 1)
        int32_t en{0};       // EN (type 2)
        int32_t eg{0};       // EG (type 3)
        int32_t em{0};       // EM (type 4)
        
        int32_t get(int type) const {
            switch (type) {
                case 1: return ee;
                case 2: return en;
                case 3: return eg;
                case 4: return em;
                default: return 0;
            }
        }
        
        void set(int type, int32_t value) {
            switch (type) {
                case 1: ee = value; break;
                case 2: en = value; break;
                case 3: eg = value; break;
                case 4: em = value; break;
            }
        }
    } energy;
    
    // Combat state
    int32_t selectedTarget{0};
    bool isAttacking{false};
    bool inAttackRange{false};
    int32_t currentAmmoType{1};     // 1-6 for lasers
    int32_t currentRocketType{1};   // 1-3 for rockets
    
    // Config
    int32_t activeConfig{1};        // Ship configuration (1 or 2)
    bool inSafeZone{false};
    
    // Timing
    using TimePoint = std::chrono::steady_clock::time_point;
    TimePoint lastUpdateTime;
    TimePoint lastCoordUpdateTime;
    
    //--------------------------------------------------------------------------
    // Helpers
    //--------------------------------------------------------------------------
    
    float healthPercent() const {
        return maxHealth > 0 ? (static_cast<float>(health) / maxHealth) * 100.0f : 0.0f;
    }
    
    float shieldPercent() const {
        return maxShield > 0 ? (static_cast<float>(shield) / maxShield) * 100.0f : 0.0f;
    }
    
    float cargoPercent() const {
        return maxCargo > 0 ? (static_cast<float>(cargo) / maxCargo) * 100.0f : 0.0f;
    }
    
    bool isCargoFull() const {
        return cargo >= maxCargo - 5;
    }
    
    bool isLowHealth(float threshold = 30.0f) const {
        return healthPercent() < threshold;
    }
    
    bool isShieldDown() const {
        return shield <= 0;
    }
    
    float distanceTo(float ox, float oy) const {
        float dx = x - ox;
        float dy = y - oy;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    // Get position (use predicted if available)
    float currentX() const { return x; }
    float currentY() const { return y; }
    
    // Get total laser ammo count
    int32_t totalLaserAmmo() const {
        return lasers.rlx1 + lasers.glx2 + lasers.blx3 + 
               lasers.wlx4 + lasers.glx2as + lasers.mrs6x;
    }
    
    // Get total rocket ammo count
    int32_t totalRocketAmmo() const {
        return rockets.kep410 + rockets.nc30 + rockets.tnc130;
    }
    
    // Interpolate position based on movement
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
    
    void reset() {
        *this = Hero{};
    }
};

/**
 * @brief Immutable snapshot of hero state
 * 
 * Thread-safe copy for bot logic to read without locks.
 */
struct HeroSnapshot {
    int32_t id{0};
    std::string name;
    std::string clanTag;
    int32_t fraction{0};
    
    float x{0}, y{0};
    float targetX{0}, targetY{0};
    bool isMoving{false};
    
    int32_t health{0}, maxHealth{0};
    int32_t shield{0}, maxShield{0};
    int32_t speed{0};
    
    // War Universe resources
    int64_t btc{0};
    int64_t plt{0};
    int64_t experience{0};
    int32_t honor{0};
    int32_t level{0};
    int32_t bootyKeys{0};
    
    int32_t cargo{0};
    int32_t maxCargo{0};
    
    // Ammo snapshots
    Hero::LaserAmmo lasers;
    Hero::RocketAmmo rockets;
    Hero::Energy energy;
    
    int32_t selectedTarget{0};
    bool isAttacking{false};
    bool inAttackRange{false};
    int32_t currentAmmoType{1};
    int32_t currentRocketType{1};
    int32_t activeConfig{1};
    bool inSafeZone{false};
    
    // Helpers
    float healthPercent() const {
        return maxHealth > 0 ? (static_cast<float>(health) / maxHealth) * 100.0f : 0.0f;
    }
    
    float shieldPercent() const {
        return maxShield > 0 ? (static_cast<float>(shield) / maxShield) * 100.0f : 0.0f;
    }
    
    float cargoPercent() const {
        return maxCargo > 0 ? (static_cast<float>(cargo) / maxCargo) * 100.0f : 0.0f;
    }
    
    bool isCargoFull() const {
        return cargo >= maxCargo - 5;
    }
    
    bool isLowHealth(float threshold = 30.0f) const {
        return healthPercent() < threshold;
    }
    
    float distanceTo(float ox, float oy) const {
        float dx = x - ox;
        float dy = y - oy;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    int32_t totalLaserAmmo() const {
        return lasers.rlx1 + lasers.glx2 + lasers.blx3 + 
               lasers.wlx4 + lasers.glx2as + lasers.mrs6x;
    }
    
    int32_t totalRocketAmmo() const {
        return rockets.kep410 + rockets.nc30 + rockets.tnc130;
    }
    
    // Create from Hero
    static HeroSnapshot from(const Hero& h) {
        HeroSnapshot snap;
        snap.id = h.id;
        snap.name = h.name;
        snap.clanTag = h.clanTag;
        snap.fraction = h.fraction;
        snap.x = h.currentX();
        snap.y = h.currentY();
        snap.targetX = h.targetX;
        snap.targetY = h.targetY;
        snap.isMoving = h.isMoving;
        snap.health = h.health;
        snap.maxHealth = h.maxHealth;
        snap.shield = h.shield;
        snap.maxShield = h.maxShield;
        snap.speed = h.speed;
        snap.btc = h.btc;
        snap.plt = h.plt;
        snap.experience = h.experience;
        snap.honor = h.honor;
        snap.level = h.level;
        snap.bootyKeys = h.bootyKeys;
        snap.cargo = h.cargo;
        snap.maxCargo = h.maxCargo;
        snap.lasers = h.lasers;
        snap.rockets = h.rockets;
        snap.energy = h.energy;
        snap.selectedTarget = h.selectedTarget;
        snap.isAttacking = h.isAttacking;
        snap.inAttackRange = h.inAttackRange;
        snap.currentAmmoType = h.currentAmmoType;
        snap.currentRocketType = h.currentRocketType;
        snap.activeConfig = h.activeConfig;
        snap.inSafeZone = h.inSafeZone;
        return snap;
    }
};

} // namespace dynamo
