#pragma once

/**
 * @file map_graph.hpp
 * @brief War Universe map connectivity graph for navigation
 * 
 * Defines portal connections between maps for pathfinding.
 * Based on wupacket-main mapRegions.js
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <queue>
#include <unordered_set>

namespace dynamo {

/**
 * @brief Portal connection info
 */
struct PortalConnection {
    double x;
    double y;
    std::string targetMap;
};

/**
 * @brief Path step for navigation
 */
struct PathStep {
    std::string fromMap;
    double portalX;
    double portalY;
    std::string toMap;
};

/**
 * @brief War Universe map graph for inter-map navigation
 * 
 * Contains all portal connections between maps.
 * Uses BFS to find shortest path between any two maps.
 */
class MapGraph {
public:
    MapGraph() {
        initializeGraph();
    }
    
    /**
     * @brief Find path from current map to destination
     * 
     * @param fromMap Current map name (e.g., "R-1")
     * @param toMap Destination map name (e.g., "R-6")
     * @param blockedMaps Maps to avoid unless they are the destination
     * @return Vector of path steps, empty if no path found
     */
    [[nodiscard]] std::vector<PathStep> findPath(const std::string& fromMap, 
                                                 const std::string& toMap,
                                                 const std::unordered_set<std::string>& blockedMaps = {}) const {
        if (fromMap == toMap) {
            return {};  // Already there
        }
        
        // BFS to find shortest path
        std::queue<std::pair<std::string, std::vector<PathStep>>> queue;
        std::unordered_set<std::string> visited;
        
        queue.push({fromMap, {}});
        visited.insert(fromMap);
        
        while (!queue.empty()) {
            auto [currentMap, path] = queue.front();
            queue.pop();
            
            auto it = mapConnections_.find(currentMap);
            if (it == mapConnections_.end()) {
                continue;  // No portals from this map
            }
            
            for (const auto& portal : it->second) {
                if (portal.targetMap != toMap &&
                    blockedMaps.find(portal.targetMap) != blockedMaps.end()) {
                    continue;
                }

                if (portal.targetMap == toMap) {
                    // Found destination!
                    path.push_back({currentMap, portal.x, portal.y, portal.targetMap});
                    return path;
                }
                
                if (visited.find(portal.targetMap) == visited.end()) {
                    visited.insert(portal.targetMap);
                    
                    auto newPath = path;
                    newPath.push_back({currentMap, portal.x, portal.y, portal.targetMap});
                    queue.push({portal.targetMap, newPath});
                }
            }
        }
        
        return {};  // No path found
    }
    
    /**
     * @brief Get next portal to use for traveling to destination
     * 
     * @param fromMap Current map
     * @param toMap Destination map
     * @param blockedMaps Maps to avoid unless they are the destination
     * @return Portal position or nullopt if no path
     */
    [[nodiscard]] std::optional<std::pair<double, double>> 
    getNextPortal(const std::string& fromMap,
                  const std::string& toMap,
                  const std::unordered_set<std::string>& blockedMaps = {}) const {
        auto path = findPath(fromMap, toMap, blockedMaps);
        if (path.empty()) {
            return std::nullopt;
        }
        return std::make_pair(path[0].portalX, path[0].portalY);
    }
    
    /**
     * @brief Get all portals on a map
     */
    [[nodiscard]] std::vector<PortalConnection> getPortals(const std::string& mapName) const {
        auto it = mapConnections_.find(mapName);
        if (it != mapConnections_.end()) {
            return it->second;
        }
        return {};
    }
    
    /**
     * @brief Check if map exists in graph
     */
    [[nodiscard]] bool hasMap(const std::string& mapName) const {
        return mapConnections_.find(mapName) != mapConnections_.end();
    }
    
    /**
     * @brief Check if direct connection exists
     */
    [[nodiscard]] bool hasDirectConnection(const std::string& fromMap, 
                                            const std::string& toMap) const {
        auto it = mapConnections_.find(fromMap);
        if (it == mapConnections_.end()) return false;
        
        for (const auto& portal : it->second) {
            if (portal.targetMap == toMap) return true;
        }
        return false;
    }
    
    /**
     * @brief Get number of hops between maps
     */
    [[nodiscard]] int getDistance(const std::string& fromMap, 
                                  const std::string& toMap,
                                  const std::unordered_set<std::string>& blockedMaps = {}) const {
        auto path = findPath(fromMap, toMap, blockedMaps);
        return static_cast<int>(path.size());
    }

private:
    std::unordered_map<std::string, std::vector<PortalConnection>> mapConnections_;
    
    void initializeGraph() {
        // Based on wupacket-main mapRegions.js
        // War Universe map connections
        
        // R (Red/Mars) faction maps
        mapConnections_["R-1"] = {
            {1000, 1000, "R-2"},
            {1000, 9000, "R-3"}
        };
        
        mapConnections_["R-2"] = {
            {8000, 9000, "R-3"},
            {1000, 1000, "J-VS"},
            {15000, 1000, "R-1"}
        };
        
        mapConnections_["R-3"] = {
            {1000, 11500, "E-3"},
            {10000, 11500, "T-1"},
            {19000, 11500, "J-SO"},
            {1000, 7500, "U-3"},
            {19000, 1000, "R-2"}
        };
        
        mapConnections_["R-5"] = {
            {1000, 11500, "R-7"},
            {10000, 11500, "R-6"},
            {1000, 1000, "G-1"},
            {19000, 1000, "T-1"}
        };
        
        mapConnections_["R-6"] = {
            {1000, 17700, "R-7"},
            {29000, 1000, "R-5"}
        };
        
        mapConnections_["R-7"] = {
            {15000, 9000, "R-6"},
            {15000, 1000, "R-5"}
        };
        
        // E (Blue/Earth) faction maps
        mapConnections_["E-1"] = {
            {15000, 1000, "E-2"},
            {1000, 1000, "E-3"}
        };
        
        mapConnections_["E-2"] = {
            {15000, 5000, "E-1"},
            {1000, 9000, "E-3"},
            {8000, 1000, "J-SO"}
        };
        
        mapConnections_["E-3"] = {
            {19000, 11500, "E-2"},
            {10000, 1000, "R-3"},
            {1000, 11500, "J-VO"},
            {1000, 1000, "T-1"}
        };
        
        mapConnections_["E-5"] = {
            {1000, 1000, "T-1"},
            {1000, 11500, "G-1"},
            {19000, 11500, "E-6"},
            {19000, 6300, "E-7"}
        };
        
        mapConnections_["E-6"] = {
            {1000, 1000, "E-5"},
            {29000, 17700, "E-7"}
        };
        
        mapConnections_["E-7"] = {
            {15000, 1000, "E-5"},
            {1000, 1000, "E-6"}
        };
        
        // U (Green/Venus) faction maps
        mapConnections_["U-1"] = {
            {1000, 9000, "U-2"},
            {15000, 1000, "U-3"}
        };
        
        mapConnections_["U-2"] = {
            {15000, 9000, "J-VO"},
            {15000, 1000, "U-3"},
            {1000, 1000, "U-1"}
        };
        
        mapConnections_["U-3"] = {
            {1000, 11500, "U-2"},
            {19000, 11500, "T-1"},
            {19000, 6500, "R-3"},
            {19000, 1000, "J-VS"}
        };
        
        mapConnections_["U-5"] = {
            {19000, 11500, "T-1"},
            {1000, 11500, "G-1"},
            {1000, 6300, "U-6"},
            {1000, 1000, "U-7"}
        };
        
        mapConnections_["U-6"] = {
            {29000, 9300, "U-5"},
            {1000, 9300, "U-7"}
        };
        
        mapConnections_["U-7"] = {
            {15000, 9000, "U-6"},
            {15000, 1000, "U-5"}
        };
        
        // Junction maps (J-XX)
        mapConnections_["J-SO"] = {
            {1000, 1000, "R-3"},
            {35000, 21500, "E-2"}
        };
        
        mapConnections_["J-VO"] = {
            {35000, 21500, "E-3"},
            {1000, 1000, "U-2"}
        };
        
        mapConnections_["J-VS"] = {
            {1000, 21500, "U-3"},
            {35000, 1000, "R-2"}
        };
        
        // PvP/Special maps
        mapConnections_["T-1"] = {
            {16000, 17000, "R-5"},
            {4800, 8500, "U-5"},
            {27300, 8500, "E-5"},
            {16000, 6000, "R-3"},
            {14300, 3000, "U-3"},
            {17800, 3000, "E-3"}
        };
        
        mapConnections_["G-1"] = {
            {20000, 8800, "T-1"}
        };
    }
};

} // namespace dynamo
