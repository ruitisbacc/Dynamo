#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace dynamo::host {

struct BackendConnectRequest {
    std::string username;
    std::string password;
    std::string serverId{"eu1"};
    std::string language{"en"};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    BackendConnectRequest,
    username,
    password,
    serverId,
    language
)

struct CommandResult {
    std::uint32_t requestId{0};
    std::uint32_t commandType{0};
    bool success{false};
    std::string message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    CommandResult,
    requestId,
    commandType,
    success,
    message
)

struct MapEntity {
    int id{0};
    int x{0};
    int y{0};
    int type{0}; // 0=npc, 1=enemy, 2=ally, 3=box, 4=portal, 5=station
    std::string name;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MapEntity, id, x, y, type, name)

struct InventoryStatsSnapshot {
    long long plt{0};
    long long btc{0};
    long long experience{0};
    long long honor{0};
    long long laserRlx1{0};
    long long laserGlx2{0};
    long long laserBlx3{0};
    long long laserWlx4{0};
    long long laserGlx2As{0};
    long long laserMrs6X{0};
    long long rocketKep410{0};
    long long rocketNc30{0};
    long long rocketTnc130{0};
    long long energyEe{0};
    long long energyEn{0};
    long long energyEg{0};
    long long energyEm{0};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    InventoryStatsSnapshot,
    plt,
    btc,
    experience,
    honor,
    laserRlx1,
    laserGlx2,
    laserBlx3,
    laserWlx4,
    laserGlx2As,
    laserMrs6X,
    rocketKep410,
    rocketNc30,
    rocketTnc130,
    energyEe,
    energyEn,
    energyEg,
    energyEm
)

struct ResourceInventorySnapshot {
    long long cerium{0};
    long long mercury{0};
    long long erbium{0};
    long long piritid{0};
    long long darkonit{0};
    long long uranit{0};
    long long azurit{0};
    long long dungid{0};
    long long xureon{0};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ResourceInventorySnapshot,
    cerium,
    mercury,
    erbium,
    piritid,
    darkonit,
    uranit,
    azurit,
    dungid,
    xureon
)

struct SessionStatsSnapshot {
    long long runtimeMs{0};
    InventoryStatsSnapshot session;
    InventoryStatsSnapshot total;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    SessionStatsSnapshot,
    runtimeMs,
    session,
    total
)

struct EnrichmentModuleSnapshot {
    std::string module;
    std::string material;
    int amount{0};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EnrichmentModuleSnapshot, module, material, amount)

struct BackendStatusSnapshot {
    std::string connectionState{"Disconnected"};
    std::string engineState{"NotConnected"};
    std::string engineError;
    std::string activeProfile{"default"};
    std::string workingMap{"-"};
    std::string currentMap{"-"};
    std::string currentMode{"Idle"};
    std::string currentTask{"Awaiting commands"};
    std::string currentTarget{"-"};
    std::string targetCategory{"-"};
    std::string safetyReason{"-"};
    std::string currentLaser{"-"};
    std::string currentRocket{"-"};
    std::string heroName;
    bool botRunning{false};
    bool botPaused{false};
    bool safetyActive{false};
    bool heroMoving{false};
    bool hasTarget{false};
    int threatCount{0};
    long long btc{0};
    long long plt{0};
    long long honor{0};
    long long experience{0};
    int hpPercent{0};
    int shieldPercent{0};
    int cargoPercent{0};
    int heroX{0};
    int heroY{0};
    int heroTargetX{0};
    int heroTargetY{0};
    int mapWidth{0};
    int mapHeight{0};
    int activeConfig{1};
    int npcCount{0};
    int enemyCount{0};
    int boxCount{0};
    int portalCount{0};
    int targetX{0};
    int targetY{0};
    int targetHpPercent{0};
    int targetShieldPercent{0};
    int targetDistance{0};
    int deathCount{0};
    std::string combatState{"Idle"};
    std::string combatDecision{"-"};
    std::string combatMovement{"-"};
    std::string travelState{"Idle"};
    std::string travelDecision{"-"};
    std::string travelDestination{"-"};
    std::string roamingDecision{"Idle"};
    ResourceInventorySnapshot currentResources;
    std::vector<EnrichmentModuleSnapshot> enrichments;
    SessionStatsSnapshot stats;
    std::vector<MapEntity> mapEntities;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    BackendStatusSnapshot,
    connectionState,
    engineState,
    engineError,
    activeProfile,
    workingMap,
    currentMap,
    currentMode,
    currentTask,
    currentTarget,
    targetCategory,
    safetyReason,
    currentLaser,
    currentRocket,
    heroName,
    botRunning,
    botPaused,
    safetyActive,
    heroMoving,
    hasTarget,
    threatCount,
    btc,
    plt,
    honor,
    experience,
    hpPercent,
    shieldPercent,
    cargoPercent,
    heroX,
    heroY,
    heroTargetX,
    heroTargetY,
    mapWidth,
    mapHeight,
    activeConfig,
    npcCount,
    enemyCount,
    boxCount,
    portalCount,
    targetX,
    targetY,
    targetHpPercent,
    targetShieldPercent,
    targetDistance,
    deathCount,
    combatState,
    combatDecision,
    combatMovement,
    travelState,
    travelDecision,
    travelDestination,
    roamingDecision,
    currentResources,
    enrichments,
    stats,
    mapEntities
)

struct ProfileListSnapshot {
    std::string activeProfile{"default"};
    std::vector<std::string> profiles;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProfileListSnapshot, activeProfile, profiles)

} // namespace dynamo::host
