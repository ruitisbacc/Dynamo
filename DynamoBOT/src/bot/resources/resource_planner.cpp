#include "bot/resources/resource_planner.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <sstream>

namespace dynamo {

namespace {

using Inventory = std::array<int32_t, kResourceTypeCount>;

struct Recipe {
    ResourceType left{ResourceType::Cerium};
    int32_t leftCount{0};
    ResourceType right{ResourceType::Mercury};
    int32_t rightCount{0};
};

struct ModuleRequest {
    ResourceModuleType module{ResourceModuleType::Lasers};
    ResourceModuleSettings settings;
    int32_t fallbackPriority{0};
};

[[nodiscard]] constexpr bool isBaseResource(ResourceType type) {
    return type == ResourceType::Cerium ||
           type == ResourceType::Mercury ||
           type == ResourceType::Erbium ||
           type == ResourceType::Piritid;
}

[[nodiscard]] constexpr std::optional<Recipe> recipeFor(ResourceType type) {
    switch (type) {
        case ResourceType::Darkonit:
            return Recipe{ResourceType::Cerium, 20, ResourceType::Mercury, 10};
        case ResourceType::Uranit:
            return Recipe{ResourceType::Mercury, 15, ResourceType::Erbium, 15};
        case ResourceType::Azurit:
            return Recipe{ResourceType::Erbium, 10, ResourceType::Piritid, 20};
        case ResourceType::Dungid:
            return Recipe{ResourceType::Darkonit, 20, ResourceType::Uranit, 20};
        case ResourceType::Xureon:
            return Recipe{ResourceType::Uranit, 20, ResourceType::Azurit, 20};
        default:
            return std::nullopt;
    }
}

[[nodiscard]] constexpr std::size_t toIndex(ResourceType type) {
    return static_cast<std::size_t>(type);
}

[[nodiscard]] constexpr std::size_t toIndex(ResourceModuleType type) {
    return static_cast<std::size_t>(type);
}

[[nodiscard]] int32_t totalCargoUnits(const Inventory& inventory) {
    int64_t total = 0;
    for (const auto amount : inventory) {
        total += amount;
    }
    return static_cast<int32_t>(std::clamp<int64_t>(total, 0, std::numeric_limits<int32_t>::max()));
}

void appendLog(std::vector<std::string>& log, const std::string& message) {
    log.push_back(message);
}

void appendRefineStep(std::vector<ResourceRefinePlanStep>& steps,
                      ResourceType target,
                      int32_t amount) {
    if (amount <= 0) {
        return;
    }

    if (!steps.empty() && steps.back().target == target) {
        steps.back().amount += amount;
        return;
    }

    steps.push_back(ResourceRefinePlanStep{target, amount});
}

[[nodiscard]] bool consumeForTarget(ResourceType target,
                                    int64_t amount,
                                    Inventory& inventory) {
    if (amount <= 0) {
        return true;
    }

    auto& slot = inventory[toIndex(target)];
    const auto useExisting = std::min<int64_t>(slot, amount);
    slot -= static_cast<int32_t>(useExisting);
    amount -= useExisting;

    if (amount == 0) {
        return true;
    }

    if (isBaseResource(target)) {
        return false;
    }

    const auto recipe = recipeFor(target);
    if (!recipe) {
        return false;
    }

    return consumeForTarget(recipe->left, static_cast<int64_t>(recipe->leftCount) * amount, inventory) &&
           consumeForTarget(recipe->right, static_cast<int64_t>(recipe->rightCount) * amount, inventory);
}

[[nodiscard]] int32_t maxProducible(ResourceType target, const Inventory& inventory) {
    int64_t high = totalCargoUnits(inventory);
    int64_t low = 0;
    int64_t best = 0;

    while (low <= high) {
        const int64_t mid = low + ((high - low) / 2);
        auto probe = inventory;
        if (consumeForTarget(target, mid, probe)) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return static_cast<int32_t>(best);
}

[[nodiscard]] bool reserveTarget(ResourceType target,
                                 int64_t amount,
                                 Inventory& inventory,
                                 std::vector<ResourceRefinePlanStep>& steps) {
    if (amount <= 0) {
        return true;
    }

    auto& slot = inventory[toIndex(target)];
    const auto useExisting = std::min<int64_t>(slot, amount);
    slot -= static_cast<int32_t>(useExisting);
    amount -= useExisting;

    if (amount == 0) {
        return true;
    }

    if (isBaseResource(target)) {
        return false;
    }

    const auto recipe = recipeFor(target);
    if (!recipe) {
        return false;
    }

    if (!reserveTarget(recipe->left, static_cast<int64_t>(recipe->leftCount) * amount, inventory, steps)) {
        return false;
    }
    if (!reserveTarget(recipe->right, static_cast<int64_t>(recipe->rightCount) * amount, inventory, steps)) {
        return false;
    }

    appendRefineStep(steps, target, static_cast<int32_t>(std::min<int64_t>(amount, std::numeric_limits<int32_t>::max())));
    return true;
}

[[nodiscard]] bool runtimeAllowsMaterial(const EnrichmentStateSnapshot* current,
                                         ResourceType target) {
    if (!current || current->possibleResources.empty()) {
        return true;
    }

    const auto value = static_cast<int32_t>(target);
    return std::find(
               current->possibleResources.begin(),
               current->possibleResources.end(),
               value) != current->possibleResources.end();
}

[[nodiscard]] std::vector<ModuleRequest> orderedModules(const ResourceAutomationSettings& settings) {
    std::vector<ModuleRequest> modules;
    modules.reserve(4);
    modules.push_back(ModuleRequest{
        ResourceModuleType::Speed,
        settings.speed,
        defaultResourcePriority(ResourceModuleType::Speed),
    });
    modules.push_back(ModuleRequest{
        ResourceModuleType::Shields,
        settings.shields,
        defaultResourcePriority(ResourceModuleType::Shields),
    });
    modules.push_back(ModuleRequest{
        ResourceModuleType::Lasers,
        settings.lasers,
        defaultResourcePriority(ResourceModuleType::Lasers),
    });
    modules.push_back(ModuleRequest{
        ResourceModuleType::Rockets,
        settings.rockets,
        defaultResourcePriority(ResourceModuleType::Rockets),
    });

    std::stable_sort(
        modules.begin(),
        modules.end(),
        [](const ModuleRequest& lhs, const ModuleRequest& rhs) {
            if (lhs.settings.priority != rhs.settings.priority) {
                return lhs.settings.priority < rhs.settings.priority;
            }
            return lhs.fallbackPriority < rhs.fallbackPriority;
        }
    );

    return modules;
}

std::string makeReserveMessage(const ResourceModulePlan& modulePlan) {
    std::ostringstream out;
    out << resourceModuleTypeName(modulePlan.module)
        << " priority " << modulePlan.priority
        << " reserved " << modulePlan.plannedAmount
        << " " << resourceTypeName(modulePlan.material);
    return out.str();
}

std::string makeBlockedMessage(const ResourceModulePlan& modulePlan) {
    std::ostringstream out;
    out << resourceModuleTypeName(modulePlan.module) << " skipped";
    if (modulePlan.blockedByActiveDifferentMaterial) {
        out << " because active "
            << modulePlan.activeMaterialAmount
            << " of type " << modulePlan.activeMaterialType
            << " is still running";
    } else if (modulePlan.blockedByServerRestrictions) {
        out << " because runtime resource panel does not allow "
            << resourceTypeName(modulePlan.material);
    } else {
        out << " because no " << resourceTypeName(modulePlan.material) << " can be produced";
    }
    return out.str();
}

void planCompression(Inventory& inventory,
                     std::vector<ResourceRefinePlanStep>& steps,
                     std::vector<std::string>& decisionLog) {
    static constexpr std::array<ResourceType, 5> compressionOrder = {
        ResourceType::Xureon,
        ResourceType::Dungid,
        ResourceType::Azurit,
        ResourceType::Uranit,
        ResourceType::Darkonit,
    };

    for (const auto target : compressionOrder) {
        const int32_t amount = maxProducible(target, inventory);
        if (amount <= 0) {
            continue;
        }

        if (!reserveTarget(target, amount, inventory, steps)) {
            continue;
        }

        std::ostringstream out;
        out << "Compression planned " << amount << " " << resourceTypeName(target);
        appendLog(decisionLog, out.str());
    }
}

} // namespace

ResourceAutomationPlan ResourcePlanner::build(const ResourceAutomationSettings& settings,
                                              const ResourceStateSnapshot& state,
                                              bool cargoFull) {
    ResourceAutomationPlan plan;
    plan.automationEnabled = settings.enabled;
    plan.cargoFull = cargoFull;
    plan.sellWhenBlocked = settings.sellWhenBlocked;

    for (std::size_t i = 0; i < kResourceTypeCount; ++i) {
        plan.initialInventory[i] = state.resources[i].amount;
    }
    plan.projectedInventory = plan.initialInventory;
    plan.initialCargoUnits = totalCargoUnits(plan.initialInventory);
    plan.projectedCargoUnits = plan.initialCargoUnits;

    if (!settings.enabled) {
        appendLog(plan.decisionLog, "Resource automation disabled.");
        return plan;
    }

    if (!state.hasResourcesInfo) {
        appendLog(plan.decisionLog, "Resource info missing; planner skipped.");
        return plan;
    }

    auto inventory = plan.initialInventory;
    const auto modules = orderedModules(settings);

    for (const auto& module : modules) {
        ResourceModulePlan modulePlan;
        modulePlan.module = module.module;
        modulePlan.priority = module.settings.priority;
        modulePlan.enabled = module.settings.enabled;
        modulePlan.material = toResourceType(module.settings.material);

        const auto* current = state.findEnrichment(static_cast<int32_t>(module.module));
        if (current) {
            modulePlan.activeMaterialType = current->type;
            modulePlan.activeMaterialAmount = current->amount;
        }

        if (!module.settings.enabled) {
            appendLog(plan.decisionLog, std::string(resourceModuleTypeName(module.module)) + " disabled.");
            plan.modulePlans.push_back(modulePlan);
            continue;
        }

        if (current && current->amount > 0 && current->type != static_cast<int32_t>(modulePlan.material)) {
            modulePlan.blockedByActiveDifferentMaterial = true;
            appendLog(plan.decisionLog, makeBlockedMessage(modulePlan));
            plan.modulePlans.push_back(modulePlan);
            continue;
        }

        if (!runtimeAllowsMaterial(current, modulePlan.material)) {
            modulePlan.blockedByServerRestrictions = true;
            appendLog(plan.decisionLog, makeBlockedMessage(modulePlan));
            plan.modulePlans.push_back(modulePlan);
            continue;
        }

        const auto maxAmount = maxProducible(modulePlan.material, inventory);
        if (maxAmount <= 0) {
            appendLog(plan.decisionLog, makeBlockedMessage(modulePlan));
            plan.modulePlans.push_back(modulePlan);
            continue;
        }

        if (!reserveTarget(modulePlan.material, maxAmount, inventory, plan.targetedRefineSteps)) {
            appendLog(plan.decisionLog, makeBlockedMessage(modulePlan));
            plan.modulePlans.push_back(modulePlan);
            continue;
        }

        modulePlan.plannedAmount = maxAmount;
        plan.enrichSteps.push_back(ResourceEnrichPlanStep{
            module.module,
            modulePlan.material,
            maxAmount,
            modulePlan.priority,
        });
        appendLog(plan.decisionLog, makeReserveMessage(modulePlan));
        plan.modulePlans.push_back(modulePlan);
    }

    planCompression(inventory, plan.compressionRefineSteps, plan.decisionLog);

    plan.projectedInventory = inventory;
    plan.projectedCargoUnits = totalCargoUnits(inventory);
    plan.freedCargoUnits = std::max(0, plan.initialCargoUnits - plan.projectedCargoUnits);
    plan.hasActions =
        !plan.targetedRefineSteps.empty() ||
        !plan.compressionRefineSteps.empty() ||
        !plan.enrichSteps.empty();
    plan.needsSellTrip =
        cargoFull &&
        settings.sellWhenBlocked &&
        plan.freedCargoUnits <= 0;

    if (plan.needsSellTrip) {
        appendLog(plan.decisionLog, "No cargo can be freed by refine/enrich; sell trip required.");
    } else if (cargoFull && plan.freedCargoUnits > 0) {
        std::ostringstream out;
        out << "Planner can free " << plan.freedCargoUnits << " cargo units.";
        appendLog(plan.decisionLog, out.str());
    } else if (cargoFull && !settings.sellWhenBlocked) {
        appendLog(plan.decisionLog, "Cargo is full and sell is disabled; bot will remain blocked after planner work.");
    }

    return plan;
}

} // namespace dynamo
