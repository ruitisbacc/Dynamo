#pragma once

/**
 * @file npc_database.hpp
 * @brief War Universe NPC and collectible database
 * 
 * Contains all known NPC names, spawn maps, and box types.
 * Based on wupacket-main renderer.js NPC_TYPES
 */

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <initializer_list>

namespace dynamo {

/**
 * @brief Box/collectible types
 */
enum class BoxType : int32_t {
    BonusBox = 0,       // Yellow bonus box (credits, ammo)
    CargoBox = 1,       // Cargo box
    EnergyBox = 2,      // Energy box
    GreenBox = 3,       // Green booty box (requires key)
};

/**
 * @brief NPC information with spawn maps
 */
struct NpcInfo {
    std::string name;
    std::vector<std::string> spawnMaps;
    
    NpcInfo(std::string n, std::initializer_list<std::string> maps)
        : name(std::move(n)), spawnMaps(maps) {}
};

/**
 * @brief All known War Universe NPCs with spawn locations
 * 
 * Map naming: R=Solar Conglomerate, E=Orion Empire, U=Vega Union
 * Numbers 1-4 = lower maps (near base), 5-7 = battle maps
 * J-SO/J-VO/J-VS = pirate sectors, G-1 = galaxy gate
 * 
 * Based on WarUniverse Wiki data (waruniverse.fandom.com)
 */
inline const std::vector<NpcInfo> ALL_NPCS = {
    // Hydro - weakest NPC, maps X-1 and X-2
    { "-=(Hydro)=-",         { "R-1", "R-2", "E-1", "E-2", "U-1", "U-2" } },
    { "-=(Hyper|Hydro)=-",   { "R-1", "R-2", "E-1", "E-2", "U-1", "U-2" } },
    { "-=(Ultra|Hydro)=-",   { "R-2", "E-2", "U-2" } },
    
    // Jenta - maps X-2 and X-3
    { "-=(Jenta)=-",         { "R-2", "R-3", "E-2", "E-3", "U-2", "U-3" } },
    { "-=(Hyper|Jenta)=-",   { "R-2", "R-3", "E-2", "E-3", "U-2", "U-3" } },
    { "-=(Ultra|Jenta)=-",   { "R-3", "E-3", "U-3" } },
    
    // Mali - maps X-3 and X-4
    { "-=(Mali)=-",          { "R-3", "R-4", "E-3", "E-4", "U-3", "U-4" } },
    { "-=(Hyper|Mali)=-",    { "R-3", "R-4", "E-3", "E-4", "U-3", "U-4" } },
    { "-=(Ultra|Mali)=-",    { "R-4", "E-4", "U-4" } },
    
    // Plarion - maps X-3 and X-4
    { "-=(Plarion)=-",       { "R-3", "R-4", "E-3", "E-4", "U-3", "U-4" } },
    { "-=(Hyper|Plarion)=-", { "R-3", "R-4", "E-3", "E-4", "U-3", "U-4" } },
    { "-=(Ultra|Plarion)=-", { "R-4", "E-4", "U-4" } },
    
    // Motron - maps X-4 and X-5
    { "-=(Motron)=-",        { "R-4", "R-5", "E-4", "E-5", "U-4", "U-5" } },
    { "-=(Hyper|Motron)=-",  { "R-4", "R-5", "E-4", "E-5", "U-4", "U-5" } },
    { "-=(Ultra|Motron)=-",  { "R-5", "E-5", "U-5" } },
    
    // Xeon - maps X-4 and X-5
    { "-=(Xeon)=-",          { "R-4", "R-5", "E-4", "E-5", "U-4", "U-5" } },
    { "-=(Hyper|Xeon)=-",    { "R-4", "R-5", "E-4", "E-5", "U-4", "U-5" } },
    { "-=(Ultra|Xeon)=-",    { "R-5", "E-5", "U-5" } },
    
    // Bangoliour - maps X-5 and X-6
    { "-=(Bangoliour)=-",       { "R-5", "R-6", "E-5", "E-6", "U-5", "U-6" } },
    { "-=(Hyper|Bangoliour)=-", { "R-5", "R-6", "E-5", "E-6", "U-5", "U-6" } },
    { "-=(Ultra|Bangoliour)=-", { "R-6", "E-6", "U-6" } },
    
    // Zavientos - maps X-6
    { "-=(Zavientos)=-",        { "R-6", "E-6", "U-6" } },
    { "-=(Hyper|Zavientos)=-",  { "R-6", "E-6", "U-6" } },
    { "-=(Ultra|Zavientos)=-",  { "R-6", "E-6", "U-6" } },
    
    // Magmius - maps X-6
    { "-=(Magmius)=-",          { "R-6", "E-6", "U-6" } },
    { "-=(Hyper|Magmius)=-",    { "R-6", "E-6", "U-6" } },
    { "-=(Ultra|Magmius)=-",    { "R-6", "E-6", "U-6" } },
    
    // Raider - pirate NPC, maps X-7 and J-sectors
    { "-=(Raider)=-",        { "R-7", "E-7", "U-7", "J-SO", "J-VO", "J-VS" } },
    { "-=(Hyper|Raider)=-",  { "R-7", "E-7", "U-7", "J-SO", "J-VO", "J-VS" } },
    { "-=(Ultra|Raider)=-",  { "J-SO", "J-VO", "J-VS" } },
    
    // Vortex - elite NPC, J-sectors
    { "-=(Vortex)=-",        { "J-SO", "J-VO", "J-VS" } },
    { "-=(Hyper|Vortex)=-",  { "J-SO", "J-VO", "J-VS" } },
    { "-=(Ultra|Vortex)=-",  { "J-SO", "J-VO", "J-VS" } },
    
    // Quattroid - boss NPC, galaxy gate
    { "-=(Quattroid)=-",       { "G-1" } },
    { "-=(Hyper|Quattroid)=-", { "G-1" } },
    { "-=(Ultra|Quattroid)=-", { "G-1" } },
};

/**
 * @brief Map from NPC name to spawn maps for quick lookup
 */
inline std::unordered_map<std::string, std::vector<std::string>> createNpcSpawnMap() {
    std::unordered_map<std::string, std::vector<std::string>> map;
    for (const auto& npc : ALL_NPCS) {
        map[npc.name] = npc.spawnMaps;
    }
    return map;
}

inline const std::unordered_map<std::string, std::vector<std::string>> NPC_SPAWN_MAP = createNpcSpawnMap();

/**
 * @brief Set of NPC names for quick lookup
 */
inline std::unordered_set<std::string> createNpcNameSet() {
    std::unordered_set<std::string> set;
    for (const auto& npc : ALL_NPCS) {
        set.insert(npc.name);
    }
    return set;
}

inline const std::unordered_set<std::string> NPC_NAME_SET = createNpcNameSet();

/**
 * @brief Check if name is a known NPC
 */
inline bool isKnownNpc(const std::string& name) {
    return NPC_NAME_SET.find(name) != NPC_NAME_SET.end();
}

/**
 * @brief Check if name looks like an NPC (has -=( prefix)
 */
inline bool looksLikeNpc(const std::string& name) {
    return name.size() > 4 && 
           name.substr(0, 3) == "-=(" && 
           name.substr(name.size() - 3) == ")=-";
}

/**
 * @brief Get spawn maps for an NPC
 * @return Vector of map names, empty if NPC not found
 */
inline std::vector<std::string> getNpcSpawnMaps(const std::string& npcName) {
    auto it = NPC_SPAWN_MAP.find(npcName);
    if (it != NPC_SPAWN_MAP.end()) {
        return it->second;
    }
    return {};
}

/**
 * @brief Check if NPC spawns on a specific map
 */
inline bool npcSpawnsOnMap(const std::string& npcName, const std::string& mapName) {
    auto it = NPC_SPAWN_MAP.find(npcName);
    if (it != NPC_SPAWN_MAP.end()) {
        const auto& maps = it->second;
        return std::find(maps.begin(), maps.end(), mapName) != maps.end();
    }
    return false;
}

/**
 * @brief Get all NPCs that spawn on a specific map
 */
inline std::vector<std::string> getNpcsOnMap(const std::string& mapName) {
    std::vector<std::string> result;
    for (const auto& npc : ALL_NPCS) {
        if (std::find(npc.spawnMaps.begin(), npc.spawnMaps.end(), mapName) != npc.spawnMaps.end()) {
            result.push_back(npc.name);
        }
    }
    return result;
}

} // namespace dynamo
