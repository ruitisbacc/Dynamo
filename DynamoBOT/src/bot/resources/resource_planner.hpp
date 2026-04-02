#pragma once

#include "bot/core/bot_config.hpp"
#include "game/resource_state.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dynamo {

struct ResourceRefinePlanStep {
    ResourceType target{ResourceType::Darkonit};
    int32_t amount{0};
};

struct ResourceEnrichPlanStep {
    ResourceModuleType module{ResourceModuleType::Lasers};
    ResourceType material{ResourceType::Darkonit};
    int32_t amount{0};
    int32_t priority{0};
};

struct ResourceModulePlan {
    ResourceModuleType module{ResourceModuleType::Lasers};
    ResourceType material{ResourceType::Darkonit};
    int32_t priority{0};
    bool enabled{false};
    bool blockedByActiveDifferentMaterial{false};
    bool blockedByServerRestrictions{false};
    int32_t activeMaterialType{-1};
    int32_t activeMaterialAmount{0};
    int32_t plannedAmount{0};
    std::vector<ResourceRefinePlanStep> targetedRefineSteps;
    bool hasEnrichStep{false};
    ResourceEnrichPlanStep enrichStep{};
    std::array<int32_t, kResourceTypeCount> projectedInventoryAfter{};
    int32_t projectedCargoUnitsAfter{0};
};

struct ResourceAutomationPlan {
    bool automationEnabled{false};
    bool cargoFull{false};
    bool sellWhenBlocked{false};
    bool needsSellTrip{false};
    bool hasActions{false};
    int32_t initialCargoUnits{0};
    int32_t projectedCargoUnits{0};
    int32_t freedCargoUnits{0};
    std::array<int32_t, kResourceTypeCount> initialInventory{};
    std::array<int32_t, kResourceTypeCount> projectedInventory{};
    std::vector<ResourceRefinePlanStep> targetedRefineSteps;
    std::vector<ResourceRefinePlanStep> compressionRefineSteps;
    std::vector<ResourceEnrichPlanStep> enrichSteps;
    std::vector<ResourceModulePlan> modulePlans;
    std::vector<std::string> decisionLog;
};

class ResourcePlanner {
public:
    [[nodiscard]] static ResourceAutomationPlan build(
        const ResourceAutomationSettings& settings,
        const ResourceStateSnapshot& state,
        bool cargoFull);
};

} // namespace dynamo
