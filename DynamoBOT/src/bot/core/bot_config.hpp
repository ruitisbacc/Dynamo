#pragma once

/**
 * @file bot_config.hpp
 * @brief Configuration structures for bot modules
 * 
 * Flat JSON-serializable structure using nlohmann/json.
 * The user configures all limits and thresholds here directly.
 */

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

#include "game/resource_state.hpp"

namespace dynamo {

/**
 * @brief Main bot operating mode
 */
enum class BotMode {
    Kill,           // Fight NPCs only
    Collect,        // Collect boxes only
    KillCollect     // Do both (combat + nearby high-priority collection)
};

NLOHMANN_JSON_SERIALIZE_ENUM(BotMode, {
    {BotMode::Kill, "Kill"},
    {BotMode::Collect, "Collect"},
    {BotMode::KillCollect, "KillCollect"}
})

/**
 * @brief Combat movement mode for NPC fights
 */
enum class CombatMovementMode {
    Adaptive,       // Pursuit + orbit + kite depending on range
};

NLOHMANN_JSON_SERIALIZE_ENUM(CombatMovementMode, {
    {CombatMovementMode::Adaptive, "Adaptive"},
})

enum class SafetyFleeMode {
    None,
    OnAttack,
    OnEnemySeen
};

NLOHMANN_JSON_SERIALIZE_ENUM(SafetyFleeMode, {
    {SafetyFleeMode::None, "None"},
    {SafetyFleeMode::OnAttack, "OnAttack"},
    {SafetyFleeMode::OnEnemySeen, "OnEnemySeen"}
})

enum class EnrichmentMaterial : int32_t {
    Darkonit = static_cast<int32_t>(ResourceType::Darkonit),
    Uranit = static_cast<int32_t>(ResourceType::Uranit),
    Azurit = static_cast<int32_t>(ResourceType::Azurit),
    Dungid = static_cast<int32_t>(ResourceType::Dungid),
    Xureon = static_cast<int32_t>(ResourceType::Xureon),
};

NLOHMANN_JSON_SERIALIZE_ENUM(EnrichmentMaterial, {
    {EnrichmentMaterial::Darkonit, "Darkonit"},
    {EnrichmentMaterial::Uranit, "Uranit"},
    {EnrichmentMaterial::Azurit, "Azurit"},
    {EnrichmentMaterial::Dungid, "Dungid"},
    {EnrichmentMaterial::Xureon, "Xureon"}
})

inline constexpr ResourceType toResourceType(EnrichmentMaterial material) {
    return static_cast<ResourceType>(static_cast<int32_t>(material));
}

inline constexpr int32_t defaultResourcePriority(ResourceModuleType moduleType) {
    switch (moduleType) {
        case ResourceModuleType::Speed: return 1;
        case ResourceModuleType::Shields: return 2;
        case ResourceModuleType::Lasers: return 3;
        case ResourceModuleType::Rockets: return 4;
        default: return 4;
    }
}

inline constexpr EnrichmentMaterial defaultResourceMaterial(ResourceModuleType moduleType) {
    switch (moduleType) {
        case ResourceModuleType::Speed:
        case ResourceModuleType::Shields:
            return EnrichmentMaterial::Uranit;
        case ResourceModuleType::Lasers:
        case ResourceModuleType::Rockets:
            return EnrichmentMaterial::Darkonit;
        default:
            return EnrichmentMaterial::Uranit;
    }
}

inline constexpr bool isAllowedEnrichmentMaterial(ResourceModuleType moduleType,
                                                  EnrichmentMaterial material) {
    switch (moduleType) {
        case ResourceModuleType::Lasers:
        case ResourceModuleType::Rockets:
            return material == EnrichmentMaterial::Darkonit ||
                   material == EnrichmentMaterial::Uranit ||
                   material == EnrichmentMaterial::Dungid;
        case ResourceModuleType::Shields:
        case ResourceModuleType::Speed:
            return material == EnrichmentMaterial::Uranit ||
                   material == EnrichmentMaterial::Azurit ||
                   material == EnrichmentMaterial::Xureon;
        default:
            return false;
    }
}

struct ResourceModuleSettings {
    bool enabled{false};
    EnrichmentMaterial material{EnrichmentMaterial::Uranit};
    int32_t priority{1};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ResourceModuleSettings, enabled, material, priority)

struct ResourceAutomationSettings {
    bool enabled{false};
    bool sellWhenBlocked{false};
    int32_t refineIntervalSeconds{120};
    ResourceModuleSettings lasers{false, EnrichmentMaterial::Darkonit, 3};
    ResourceModuleSettings rockets{false, EnrichmentMaterial::Darkonit, 4};
    ResourceModuleSettings shields{false, EnrichmentMaterial::Uranit, 2};
    ResourceModuleSettings speed{false, EnrichmentMaterial::Uranit, 1};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ResourceAutomationSettings, enabled, sellWhenBlocked, refineIntervalSeconds, lasers, rockets, shields, speed)

/**
 * @brief NPC target configuration
 */
struct NpcTargetConfig {
    std::string namePattern{"-=.*=-"};        // Regex or exact name match (e.g. "-=(Hydro)=-")
    int32_t priority{1};            // Higher = more preferred
    int32_t ammoType{1};            // 1-6 for laser types
    int32_t rocketType{0};          // 0=none, 1-3 for rocket types
    int32_t range{550};             // Preferred orbit/hold distance
    bool followOnLowHp{false};      // Keep chasing a low-hp target past the normal leash
    int32_t followOnLowHpPercent{25}; // Low HP threshold for follow behavior
    bool ignoreOwnership{false};    // Ignore another player's ownership/engagement
    int32_t maxDistance{2000};      // Max distance to engage (0=unlimited)
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    NpcTargetConfig,
    namePattern,
    priority,
    ammoType,
    rocketType,
    range,
    followOnLowHp,
    followOnLowHpPercent,
    ignoreOwnership,
    maxDistance
)

/**
 * @brief Box collection configuration
 */
struct BoxTargetConfig {
    int32_t type{0};                // 0=bonus, 1=cargo, 2=energy, 3=green
    int32_t priority{1};            // Higher = more preferred
    bool enabled{true};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BoxTargetConfig, type, priority, enabled)

/**
 * @brief Safety module configuration
 */
struct SafetyConfig {
    bool enabled{true};
    
    // HP thresholds (percentage)
    int32_t minHpPercent{15};           // Emergency flee threshold
    int32_t repairHpPercent{70};        // Start repair when below this
    int32_t fullHpPercent{100};         // Consider "full" above this
    
    // Enemy behavior
    SafetyFleeMode fleeMode{SafetyFleeMode::OnEnemySeen};
    int32_t enemySeenTimeoutMs{15000};  // Threat tracking freshness window
    
    // Escape settings
    bool preferPortalEscape{true};      // Prefer portals over random flee
    int32_t fleeMoveCooldownMs{200};
    int32_t adminEscapeDelayMs{300000}; // Stay defensive after admin seen (5 min)
    
    // Config switching
    bool useEscapeConfig{true};
    int32_t escapeConfigId{2};          // Ship config for escaping (1 or 2)
    int32_t fightConfigId{1};           // Ship config for fighting
    int32_t configSwitchCooldownMs{1000};
    
    int32_t priority{90};               // Module priority when active
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SafetyConfig, enabled, minHpPercent, repairHpPercent, fullHpPercent, fleeMode, enemySeenTimeoutMs, preferPortalEscape, fleeMoveCooldownMs, adminEscapeDelayMs, useEscapeConfig, escapeConfigId, fightConfigId, configSwitchCooldownMs, priority)

/**
 * @brief Combat module configuration
 */
struct CombatConfig {
    bool enabled{true};
    
    // Targeting
    std::vector<NpcTargetConfig> targets;
    bool targetEngagedNpc{false};       // Attack NPCs already being attacked
    int32_t maxCombatDistance{2000};    // Max distance to engage NPC
    int32_t attackRange{600};           // Start attacking when within this range
    
    // Combat behavior
    int32_t selectCooldownMs{250};
    int32_t attackCooldownMs{200};
    bool useRockets{false};
    int32_t rocketCooldownMs{2000};
    
    // Movement
    int32_t followDistance{500};
    int32_t moveCooldownMs{200};
    int32_t configId{1};                 // Ship config slot used during combat
    
    // Anti-ban
    bool randomMovement{true};
    int32_t antibanMoveIntervalMs{20000};

    // Distance learner (profiling tool — disable in production)

    int32_t priority{60};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CombatConfig, enabled, targets, targetEngagedNpc, maxCombatDistance, attackRange, selectCooldownMs, attackCooldownMs, useRockets, rocketCooldownMs, followDistance, moveCooldownMs, configId, randomMovement, antibanMoveIntervalMs, priority)

/**
 * @brief Collection module configuration
 */
struct CollectConfig {
    bool enabled{true};
    
    std::vector<BoxTargetConfig> targetBoxes;
    int32_t maxCollectDistance{1500};
    int32_t collectCooldownMs{500};
    int32_t moveCooldownMs{200};
    
    // Skip conditions
    bool skipBootyIfNoKeys{true};
    bool skipResourceIfCargoFull{true}; // Legacy name, used for cargo-like boxes
    
    // Collection during combat
    bool collectDuringCombat{true};
    int32_t combatCollectMaxDistance{800};
    int32_t configId{2};                 // Ship config slot used for collection/general flying
    
    int32_t priority{40};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CollectConfig, enabled, targetBoxes, maxCollectDistance, collectCooldownMs, moveCooldownMs, skipBootyIfNoKeys, skipResourceIfCargoFull, collectDuringCombat, combatCollectMaxDistance, configId, priority)

/**
 * @brief Roaming module configuration
 */
struct RoamingConfig {
    bool enabled{true};
    
    int32_t moveCooldownMs{3000};
    int32_t mapMargin{500};             // Stay this far from map edges
    int32_t configId{1};                // Ship config slot used while roaming
    
    int32_t priority{10};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RoamingConfig, enabled, moveCooldownMs, mapMargin, configId, priority)

/**
 * @brief Map navigation configuration
 */
struct MapConfig {
    bool enabled{true};
    std::string workingMap{"R-1"};      // Target map name (e.g. "R-6")
    int32_t workingMapId{0};            // Or map ID
    bool autoTravelToWorkingMap{true};
    int32_t travelConfigId{2};          // Ship config slot used while traveling
    
    // Portals to avoid
    std::vector<std::string> avoidMaps; // e.g. ["T-1", "G-1"] for PvP zones
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MapConfig, enabled, workingMap, workingMapId, autoTravelToWorkingMap, travelConfigId, avoidMaps)

/**
 * @brief Revive configuration
 */
struct ReviveConfig {
    bool enabled{true};
    int32_t waitBeforeReviveMs{3000};
    int32_t waitAfterReviveMs{5000};
    int32_t maxDeaths{0};               // 0=unlimited
    bool stopBotOnMaxDeaths{true};
    bool disconnectOnMaxDeaths{false};  // Disconnect instead of stopping when maxDeaths is exceeded
    int32_t disconnectCooldownMinutes{15};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ReviveConfig, enabled, waitBeforeReviveMs, waitAfterReviveMs, maxDeaths, stopBotOnMaxDeaths, disconnectOnMaxDeaths, disconnectCooldownMinutes)

/**
 * @brief Admin detection configuration
 */
struct AdminConfig {
    bool enabled{true};
    int32_t droneCountThreshold{8};     // Admins have >8 drones
    int32_t escapeDelayMs{300000};      // 5 minutes wait after admin seen
    bool disconnectWhenSeen{false};
    int32_t disconnectCooldownMinutes{5};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AdminConfig, enabled, droneCountThreshold, escapeDelayMs, disconnectWhenSeen, disconnectCooldownMinutes)

/**
 * @brief Autobuy configuration — automatic ammo purchasing
 */
struct AutobuyConfig {
    bool laserRlx1{false};
    bool laserGlx2{false};
    bool laserBlx3{false};
    bool laserGlx2As{false};
    bool laserMrs6x{false};
    bool rocketKep410{false};
    bool rocketNc30{false};
    bool rocketTnc130{false};

    [[nodiscard]] bool anyEnabled() const {
        return laserRlx1 || laserGlx2 || laserBlx3 || laserGlx2As || laserMrs6x
            || rocketKep410 || rocketNc30 || rocketTnc130;
    }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AutobuyConfig, laserRlx1, laserGlx2, laserBlx3, laserGlx2As, laserMrs6x, rocketKep410, rocketNc30, rocketTnc130)

/**
 * @brief Complete flat bot configuration
 */
struct BotConfig {
    SafetyConfig safety;
    CombatConfig combat;
    CollectConfig collect;
    RoamingConfig roaming;
    MapConfig map;
    ReviveConfig revive;
    AdminConfig admin;
    ResourceAutomationSettings resources;
    AutobuyConfig autobuy;

    // Global settings
    BotMode mode{BotMode::KillCollect};
    int32_t tickRateMs{16};             // fixed minimum tick interval (~60 Hz)
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BotConfig, safety, combat, collect, roaming, map, revive, admin, resources, autobuy, mode, tickRateMs)

} // namespace dynamo
