#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dynamo {

enum class ResourceType : int32_t {
    Cerium = 0,
    Mercury = 1,
    Erbium = 2,
    Piritid = 3,
    Darkonit = 4,
    Uranit = 5,
    Azurit = 6,
    Dungid = 7,
    Xureon = 8,
};

enum class ResourceModuleType : int32_t {
    Lasers = 0,
    Rockets = 1,
    Shields = 2,
    Speed = 3,
};

enum class ResourcesActionId : int32_t {
    FetchInfo = 0,
    Refine = 1,
    Enrich = 2,
    FetchTradeInfo = 3,
    Sell = 4,
};

inline constexpr std::size_t kResourceTypeCount = 9;
inline constexpr std::size_t kResourceModuleTypeCount = 4;

inline constexpr bool isValidResourceType(int32_t value) {
    return value >= 0 && value < static_cast<int32_t>(kResourceTypeCount);
}

inline constexpr bool isValidRefineTargetType(int32_t value) {
    return value >= static_cast<int32_t>(ResourceType::Darkonit) &&
           value < static_cast<int32_t>(kResourceTypeCount);
}

inline constexpr bool isValidResourceModuleType(int32_t value) {
    return value >= 0 && value < static_cast<int32_t>(kResourceModuleTypeCount);
}

inline constexpr const char* resourceTypeName(ResourceType type) {
    switch (type) {
        case ResourceType::Cerium: return "Cerium";
        case ResourceType::Mercury: return "Mercury";
        case ResourceType::Erbium: return "Erbium";
        case ResourceType::Piritid: return "Piritid";
        case ResourceType::Darkonit: return "Darkonit";
        case ResourceType::Uranit: return "Uranit";
        case ResourceType::Azurit: return "Azurit";
        case ResourceType::Dungid: return "Dungid";
        case ResourceType::Xureon: return "Xureon";
        default: return "Unknown";
    }
}

inline constexpr const char* resourceModuleTypeName(ResourceModuleType type) {
    switch (type) {
        case ResourceModuleType::Lasers: return "Lasers";
        case ResourceModuleType::Rockets: return "Rockets";
        case ResourceModuleType::Shields: return "Shields";
        case ResourceModuleType::Speed: return "Speed";
        default: return "Unknown";
    }
}

struct ResourceStackSnapshot {
    int32_t amount{0};
    int32_t maxRefineAmount{0};
};

struct EnrichmentStateSnapshot {
    int32_t amount{0};
    int32_t type{-1};
    std::vector<int32_t> possibleResources;
};

struct ResourceStateSnapshot {
    std::array<ResourceStackSnapshot, kResourceTypeCount> resources{};
    std::array<ResourceStackSnapshot, kResourceTypeCount> tradeResources{};
    std::array<EnrichmentStateSnapshot, kResourceModuleTypeCount> enrichments{};

    bool hasResourcesInfo{false};
    bool hasTradeInfo{false};
    int64_t resourcesInfoUpdatedAtMs{0};
    int64_t tradeInfoUpdatedAtMs{0};

    [[nodiscard]] const ResourceStackSnapshot* findResource(int32_t type) const {
        if (!isValidResourceType(type)) {
            return nullptr;
        }
        return &resources[static_cast<std::size_t>(type)];
    }

    [[nodiscard]] const ResourceStackSnapshot* findTradeResource(int32_t type) const {
        if (!isValidResourceType(type)) {
            return nullptr;
        }
        return &tradeResources[static_cast<std::size_t>(type)];
    }

    [[nodiscard]] const EnrichmentStateSnapshot* findEnrichment(int32_t moduleType) const {
        if (!isValidResourceModuleType(moduleType)) {
            return nullptr;
        }
        return &enrichments[static_cast<std::size_t>(moduleType)];
    }
};

} // namespace dynamo
