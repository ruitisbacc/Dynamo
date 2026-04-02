#include "bot/resources/resource_planner.hpp"

#include <algorithm>
#include <cmath>
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

struct DemandProfile {
    std::array<double, kResourceTypeCount> direct{};
    std::array<double, kResourceTypeCount> transitive{};
    std::array<double, kResourceTypeCount> utility{};
};

struct CompressionCandidate {
    ResourceType target{ResourceType::Darkonit};
    int32_t amount{0};
    std::vector<ResourceRefinePlanStep> steps;
    Inventory projectedInventory{};
    int32_t freedCargoUnits{0};
    double score{-std::numeric_limits<double>::infinity()};
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

[[nodiscard]] constexpr int resourceTier(ResourceType type) {
    switch (type) {
        case ResourceType::Cerium:
        case ResourceType::Mercury:
        case ResourceType::Erbium:
        case ResourceType::Piritid:
            return 0;
        case ResourceType::Darkonit:
        case ResourceType::Uranit:
        case ResourceType::Azurit:
            return 1;
        case ResourceType::Dungid:
        case ResourceType::Xureon:
            return 2;
        default:
            return 0;
    }
}

[[nodiscard]] constexpr std::size_t toIndex(ResourceType type) {
    return static_cast<std::size_t>(type);
}

[[nodiscard]] double priorityWeight(int32_t priority) {
    const double rank = static_cast<double>(std::clamp(6 - priority, 1, 5));
    return rank * rank;
}

[[nodiscard]] int32_t baseCostUnits(ResourceType type) {
    if (isBaseResource(type)) {
        return 1;
    }

    const auto recipe = recipeFor(type);
    if (!recipe) {
        return 0;
    }

    return recipe->leftCount * baseCostUnits(recipe->left) +
           recipe->rightCount * baseCostUnits(recipe->right);
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

void appendRefineSteps(std::vector<ResourceRefinePlanStep>& destination,
                       const std::vector<ResourceRefinePlanStep>& source) {
    for (const auto& step : source) {
        appendRefineStep(destination, step.target, step.amount);
    }
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

    appendRefineStep(
        steps,
        target,
        static_cast<int32_t>(std::min<int64_t>(amount, std::numeric_limits<int32_t>::max()))
    );
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

void addTransitiveDemand(ResourceType target, double weight, DemandProfile& profile) {
    const auto recipe = recipeFor(target);
    if (!recipe || weight <= 0.0) {
        return;
    }

    const double nextWeight = weight * 0.72;
    profile.transitive[toIndex(recipe->left)] += nextWeight;
    profile.transitive[toIndex(recipe->right)] += nextWeight;

    addTransitiveDemand(recipe->left, nextWeight * 0.88, profile);
    addTransitiveDemand(recipe->right, nextWeight * 0.88, profile);
}

[[nodiscard]] DemandProfile buildDemandProfile(const std::vector<ModuleRequest>& modules) {
    DemandProfile profile;

    for (const auto& module : modules) {
        if (!module.settings.enabled) {
            continue;
        }

        const auto material = toResourceType(module.settings.material);
        const double weight = priorityWeight(module.settings.priority);
        profile.direct[toIndex(material)] += weight;
        addTransitiveDemand(material, weight, profile);
    }

    for (std::size_t i = 0; i < kResourceTypeCount; ++i) {
        const auto resource = static_cast<ResourceType>(i);
        const double direct = profile.direct[i];
        const double transitive = profile.transitive[i];
        const double sqrtCost = std::sqrt(static_cast<double>(std::max(baseCostUnits(resource), 1)));
        const double tierBonus = static_cast<double>(resourceTier(resource)) * 3.0;

        profile.utility[i] =
            direct * (12.0 + sqrtCost * 0.9 + tierBonus) +
            transitive * (3.5 + sqrtCost * 0.25 + tierBonus * 0.4);

        if (direct <= 0.0 && transitive <= 0.0 && !isBaseResource(resource)) {
            profile.utility[i] = sqrtCost * 0.18 + tierBonus * 0.25;
        }
    }

    return profile;
}

[[nodiscard]] double strategicInventoryScore(const Inventory& inventory,
                                             const DemandProfile& demandProfile) {
    double score = 0.0;
    for (std::size_t i = 0; i < kResourceTypeCount; ++i) {
        score += static_cast<double>(inventory[i]) * demandProfile.utility[i];
    }
    return score;
}

[[nodiscard]] std::string summarizeRefineSteps(const std::vector<ResourceRefinePlanStep>& steps) {
    if (steps.empty()) {
        return "existing stock only";
    }

    std::ostringstream out;
    bool first = true;
    for (const auto& step : steps) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << step.amount << " " << resourceTypeName(step.target);
    }
    return out.str();
}

std::string makeReserveMessage(const ResourceModulePlan& modulePlan) {
    std::ostringstream out;
    out << resourceModuleTypeName(modulePlan.module)
        << " priority " << modulePlan.priority
        << " planned " << modulePlan.plannedAmount
        << " " << resourceTypeName(modulePlan.material)
        << " using " << summarizeRefineSteps(modulePlan.targetedRefineSteps);
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

[[nodiscard]] bool applyRefineStep(const ResourceRefinePlanStep& step, Inventory& inventory) {
    if (step.amount <= 0) {
        return true;
    }

    const auto recipe = recipeFor(step.target);
    if (!recipe) {
        return false;
    }

    const int64_t leftRequired = static_cast<int64_t>(recipe->leftCount) * step.amount;
    const int64_t rightRequired = static_cast<int64_t>(recipe->rightCount) * step.amount;
    auto& leftSlot = inventory[toIndex(recipe->left)];
    auto& rightSlot = inventory[toIndex(recipe->right)];
    auto& outputSlot = inventory[toIndex(step.target)];

    if (leftSlot < leftRequired || rightSlot < rightRequired) {
        return false;
    }

    leftSlot -= static_cast<int32_t>(leftRequired);
    rightSlot -= static_cast<int32_t>(rightRequired);
    outputSlot += step.amount;
    return true;
}

[[nodiscard]] bool applyEnrichStep(const ResourceEnrichPlanStep& step, Inventory& inventory) {
    if (step.amount <= 0) {
        return true;
    }

    auto& slot = inventory[toIndex(step.material)];
    if (slot < step.amount) {
        return false;
    }

    slot -= step.amount;
    return true;
}

[[nodiscard]] bool simulateModuleExecution(ResourceModulePlan& modulePlan,
                                           Inventory& inventory,
                                           std::vector<std::string>& decisionLog) {
    for (const auto& step : modulePlan.targetedRefineSteps) {
        if (!applyRefineStep(step, inventory)) {
            std::ostringstream out;
            out << "Simulation mismatch while refining "
                << step.amount << " " << resourceTypeName(step.target)
                << " for " << resourceModuleTypeName(modulePlan.module) << ".";
            appendLog(decisionLog, out.str());
            modulePlan.projectedInventoryAfter = inventory;
            modulePlan.projectedCargoUnitsAfter = totalCargoUnits(inventory);
            return false;
        }
    }

    if (modulePlan.hasEnrichStep && !applyEnrichStep(modulePlan.enrichStep, inventory)) {
        std::ostringstream out;
        out << "Simulation mismatch while enriching "
            << resourceModuleTypeName(modulePlan.module)
            << " with " << modulePlan.enrichStep.amount
            << " " << resourceTypeName(modulePlan.enrichStep.material) << ".";
        appendLog(decisionLog, out.str());
        modulePlan.projectedInventoryAfter = inventory;
        modulePlan.projectedCargoUnitsAfter = totalCargoUnits(inventory);
        return false;
    }

    modulePlan.projectedInventoryAfter = inventory;
    modulePlan.projectedCargoUnitsAfter = totalCargoUnits(inventory);
    return true;
}

[[nodiscard]] bool applyRefineSteps(const std::vector<ResourceRefinePlanStep>& steps,
                                    Inventory& inventory) {
    for (const auto& step : steps) {
        if (!applyRefineStep(step, inventory)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool hasDirectDemand(const DemandProfile& demandProfile, ResourceType type) {
    return demandProfile.direct[toIndex(type)] > 0.0;
}

[[nodiscard]] int32_t partnerSupportFor(ResourceType type, const Inventory& inventory) {
    switch (type) {
        case ResourceType::Darkonit:
            return inventory[toIndex(ResourceType::Uranit)] +
                   maxProducible(ResourceType::Uranit, inventory);
        case ResourceType::Azurit:
            return inventory[toIndex(ResourceType::Uranit)] +
                   maxProducible(ResourceType::Uranit, inventory);
        case ResourceType::Uranit:
            return inventory[toIndex(ResourceType::Darkonit)] +
                   inventory[toIndex(ResourceType::Azurit)] +
                   maxProducible(ResourceType::Darkonit, inventory) +
                   maxProducible(ResourceType::Azurit, inventory);
        default:
            return 0;
    }
}

[[nodiscard]] int32_t usefulSecondTierAmount(ResourceType type,
                                             const Inventory& inventory,
                                             const DemandProfile& demandProfile) {
    auto useful = hasDirectDemand(demandProfile, type) ? inventory[toIndex(type)] : 0;

    switch (type) {
        case ResourceType::Darkonit:
            if (hasDirectDemand(demandProfile, ResourceType::Dungid)) {
                useful = std::max(
                    useful,
                    std::min(inventory[toIndex(ResourceType::Darkonit)],
                             partnerSupportFor(ResourceType::Darkonit, inventory))
                );
            }
            break;

        case ResourceType::Azurit:
            if (hasDirectDemand(demandProfile, ResourceType::Xureon)) {
                useful = std::max(
                    useful,
                    std::min(inventory[toIndex(ResourceType::Azurit)],
                             partnerSupportFor(ResourceType::Azurit, inventory))
                );
            }
            break;

        case ResourceType::Uranit: {
            int32_t remaining = inventory[toIndex(ResourceType::Uranit)] - useful;
            if (remaining > 0 && hasDirectDemand(demandProfile, ResourceType::Dungid)) {
                const int32_t darkSupport =
                    inventory[toIndex(ResourceType::Darkonit)] +
                    maxProducible(ResourceType::Darkonit, inventory);
                const int32_t allocated = std::min(remaining, darkSupport);
                useful += allocated;
                remaining -= allocated;
            }
            if (remaining > 0 && hasDirectDemand(demandProfile, ResourceType::Xureon)) {
                const int32_t azuritSupport =
                    inventory[toIndex(ResourceType::Azurit)] +
                    maxProducible(ResourceType::Azurit, inventory);
                useful += std::min(remaining, azuritSupport);
            }
            break;
        }

        default:
            break;
    }

    return useful;
}

[[nodiscard]] double strandedSecondTierPenalty(const Inventory& inventory,
                                               const DemandProfile& demandProfile) {
    static constexpr std::array<ResourceType, 3> secondTierTypes = {
        ResourceType::Darkonit,
        ResourceType::Uranit,
        ResourceType::Azurit,
    };

    double penalty = 0.0;
    for (const auto type : secondTierTypes) {
        const int32_t amount = inventory[toIndex(type)];
        if (amount <= 0) {
            continue;
        }

        const int32_t useful = usefulSecondTierAmount(type, inventory, demandProfile);
        const int32_t stranded = std::max(0, amount - useful);
        if (stranded <= 0) {
            continue;
        }

        const double unitPenalty =
            34.0 + std::sqrt(static_cast<double>(std::max(baseCostUnits(type), 1))) * 8.0;
        penalty += static_cast<double>(stranded) * unitPenalty;
    }

    return penalty;
}

[[nodiscard]] std::vector<int32_t> compressionCandidateAmounts(ResourceType target,
                                                               const Inventory& inventory,
                                                               const DemandProfile& demandProfile) {
    std::vector<int32_t> candidates;
    const int32_t maxAmount = maxProducible(target, inventory);
    if (maxAmount <= 0) {
        return candidates;
    }

    const auto addCandidate = [&candidates](int32_t amount) {
        if (amount <= 0) {
            return;
        }
        if (std::find(candidates.begin(), candidates.end(), amount) == candidates.end()) {
            candidates.push_back(amount);
        }
    };

    addCandidate(1);

    switch (target) {
        case ResourceType::Darkonit: {
            if (hasDirectDemand(demandProfile, ResourceType::Darkonit)) {
                addCandidate(maxAmount);
            } else if (hasDirectDemand(demandProfile, ResourceType::Dungid)) {
                addCandidate(std::min(maxAmount, partnerSupportFor(ResourceType::Darkonit, inventory)));
            }
            break;
        }

        case ResourceType::Azurit: {
            if (hasDirectDemand(demandProfile, ResourceType::Azurit)) {
                addCandidate(maxAmount);
            } else if (hasDirectDemand(demandProfile, ResourceType::Xureon)) {
                addCandidate(std::min(maxAmount, partnerSupportFor(ResourceType::Azurit, inventory)));
            }
            break;
        }

        case ResourceType::Uranit: {
            if (hasDirectDemand(demandProfile, ResourceType::Uranit)) {
                addCandidate(maxAmount);
            } else {
                addCandidate(std::min(maxAmount, partnerSupportFor(ResourceType::Uranit, inventory)));
            }
            break;
        }

        case ResourceType::Dungid:
        case ResourceType::Xureon:
            addCandidate(maxAmount);
            break;

        default:
            break;
    }

    std::sort(candidates.begin(), candidates.end());
    return candidates;
}

[[nodiscard]] bool compressionUsefulnessGate(ResourceType target,
                                             const Inventory& inventoryAfter,
                                             const DemandProfile& demandProfile) {
    switch (target) {
        case ResourceType::Darkonit:
            return hasDirectDemand(demandProfile, ResourceType::Darkonit) ||
                   (hasDirectDemand(demandProfile, ResourceType::Dungid) &&
                    partnerSupportFor(ResourceType::Darkonit, inventoryAfter) > 0);
        case ResourceType::Azurit:
            return hasDirectDemand(demandProfile, ResourceType::Azurit) ||
                   (hasDirectDemand(demandProfile, ResourceType::Xureon) &&
                    partnerSupportFor(ResourceType::Azurit, inventoryAfter) > 0);
        case ResourceType::Uranit:
            return hasDirectDemand(demandProfile, ResourceType::Uranit) ||
                   (hasDirectDemand(demandProfile, ResourceType::Dungid) &&
                    (inventoryAfter[toIndex(ResourceType::Darkonit)] > 0 ||
                     maxProducible(ResourceType::Darkonit, inventoryAfter) > 0)) ||
                   (hasDirectDemand(demandProfile, ResourceType::Xureon) &&
                    (inventoryAfter[toIndex(ResourceType::Azurit)] > 0 ||
                     maxProducible(ResourceType::Azurit, inventoryAfter) > 0));
        case ResourceType::Dungid:
        case ResourceType::Xureon:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] std::optional<CompressionCandidate> evaluateCompressionCandidate(
    ResourceType target,
    int32_t amount,
    const Inventory& inventory,
    const DemandProfile& demandProfile) {
    if (amount <= 0) {
        return std::nullopt;
    }

    Inventory reservationInventory = inventory;
    std::vector<ResourceRefinePlanStep> steps;
    if (!reserveTarget(target, amount, reservationInventory, steps)) {
        return std::nullopt;
    }

    Inventory projectedInventory = inventory;
    if (!applyRefineSteps(steps, projectedInventory)) {
        return std::nullopt;
    }

    if (!compressionUsefulnessGate(target, projectedInventory, demandProfile)) {
        return std::nullopt;
    }

    const int32_t beforeCargo = totalCargoUnits(inventory);
    const int32_t afterCargo = totalCargoUnits(projectedInventory);
    const int32_t freedCargo = std::max(0, beforeCargo - afterCargo);
    if (freedCargo <= 0) {
        return std::nullopt;
    }

    const double beforeStrategic = strategicInventoryScore(inventory, demandProfile);
    const double afterStrategic = strategicInventoryScore(projectedInventory, demandProfile);
    const double beforeStranded = strandedSecondTierPenalty(inventory, demandProfile);
    const double afterStranded = strandedSecondTierPenalty(projectedInventory, demandProfile);
    const double deltaStrategic = afterStrategic - beforeStrategic;
    const double deltaStranded = std::max(0.0, afterStranded - beforeStranded);
    const double costPenalty =
        static_cast<double>(amount) *
        std::sqrt(static_cast<double>(std::max(baseCostUnits(target), 1))) *
        (hasDirectDemand(demandProfile, target) ? 0.55 : 1.35);

    CompressionCandidate candidate;
    candidate.target = target;
    candidate.amount = amount;
    candidate.steps = std::move(steps);
    candidate.projectedInventory = projectedInventory;
    candidate.freedCargoUnits = freedCargo;
    candidate.score = static_cast<double>(freedCargo) * 2.5 +
                      deltaStrategic -
                      deltaStranded -
                      costPenalty;
    return candidate;
}

void planCompression(Inventory& reservationInventory,
                     std::vector<ResourceRefinePlanStep>& steps,
                     std::vector<std::string>& decisionLog,
                     const DemandProfile& demandProfile) {
    static constexpr std::array<ResourceType, 5> compressionTargets = {
        ResourceType::Xureon,
        ResourceType::Dungid,
        ResourceType::Azurit,
        ResourceType::Uranit,
        ResourceType::Darkonit,
    };

    while (true) {
        std::optional<CompressionCandidate> best;

        for (const auto target : compressionTargets) {
            const auto candidateAmounts =
                compressionCandidateAmounts(target, reservationInventory, demandProfile);
            for (const auto amount : candidateAmounts) {
                auto candidate =
                    evaluateCompressionCandidate(target, amount, reservationInventory, demandProfile);
                if (!candidate.has_value()) {
                    continue;
                }

                if (!best.has_value() || candidate->score > best->score) {
                    best = std::move(candidate);
                }
            }
        }

        if (!best.has_value() || best->score <= 0.0) {
            return;
        }

        appendRefineSteps(steps, best->steps);
        reservationInventory = best->projectedInventory;

        std::ostringstream out;
        out << "Compression planned " << best->amount << " " << resourceTypeName(best->target)
            << " using " << summarizeRefineSteps(best->steps)
            << " (freed " << best->freedCargoUnits
            << ", score " << static_cast<int32_t>(std::round(best->score)) << ")";
        appendLog(decisionLog, out.str());
    }
}

void simulateCompression(const std::vector<ResourceRefinePlanStep>& steps,
                         Inventory& inventory,
                         std::vector<std::string>& decisionLog) {
    for (const auto& step : steps) {
        if (applyRefineStep(step, inventory)) {
            continue;
        }

        std::ostringstream out;
        out << "Simulation mismatch while applying compression step "
            << step.amount << " " << resourceTypeName(step.target) << ".";
        appendLog(decisionLog, out.str());
        return;
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

    const auto modules = orderedModules(settings);
    const auto demandProfile = buildDemandProfile(modules);
    Inventory reservationInventory = plan.initialInventory;

    for (const auto& module : modules) {
        ResourceModulePlan modulePlan;
        modulePlan.module = module.module;
        modulePlan.priority = module.settings.priority;
        modulePlan.enabled = module.settings.enabled;
        modulePlan.material = toResourceType(module.settings.material);
        modulePlan.projectedInventoryAfter = reservationInventory;
        modulePlan.projectedCargoUnitsAfter = totalCargoUnits(reservationInventory);

        const auto* current = state.findEnrichment(static_cast<int32_t>(module.module));
        if (current) {
            modulePlan.activeMaterialType = current->type;
            modulePlan.activeMaterialAmount = current->amount;
        }

        if (!module.settings.enabled) {
            appendLog(plan.decisionLog, std::string(resourceModuleTypeName(module.module)) + " disabled.");
            plan.modulePlans.push_back(std::move(modulePlan));
            continue;
        }

        if (current &&
            current->amount > 0 &&
            current->type != static_cast<int32_t>(modulePlan.material)) {
            modulePlan.blockedByActiveDifferentMaterial = true;
            appendLog(plan.decisionLog, makeBlockedMessage(modulePlan));
            plan.modulePlans.push_back(std::move(modulePlan));
            continue;
        }

        if (!runtimeAllowsMaterial(current, modulePlan.material)) {
            modulePlan.blockedByServerRestrictions = true;
            appendLog(plan.decisionLog, makeBlockedMessage(modulePlan));
            plan.modulePlans.push_back(std::move(modulePlan));
            continue;
        }

        const int32_t maxAmount = maxProducible(modulePlan.material, reservationInventory);
        if (maxAmount <= 0) {
            appendLog(plan.decisionLog, makeBlockedMessage(modulePlan));
            plan.modulePlans.push_back(std::move(modulePlan));
            continue;
        }

        Inventory probe = reservationInventory;
        if (!reserveTarget(modulePlan.material, maxAmount, probe, modulePlan.targetedRefineSteps)) {
            appendLog(plan.decisionLog, makeBlockedMessage(modulePlan));
            plan.modulePlans.push_back(std::move(modulePlan));
            continue;
        }

        reservationInventory = probe;
        modulePlan.plannedAmount = maxAmount;
        modulePlan.hasEnrichStep = true;
        modulePlan.enrichStep = ResourceEnrichPlanStep{
            module.module,
            modulePlan.material,
            maxAmount,
            modulePlan.priority,
        };

        appendRefineSteps(plan.targetedRefineSteps, modulePlan.targetedRefineSteps);
        plan.enrichSteps.push_back(modulePlan.enrichStep);
        appendLog(plan.decisionLog, makeReserveMessage(modulePlan));
        plan.modulePlans.push_back(std::move(modulePlan));
    }

    Inventory targetedProjection = plan.initialInventory;
    for (auto& modulePlan : plan.modulePlans) {
        if (!modulePlan.enabled || !modulePlan.hasEnrichStep) {
            modulePlan.projectedInventoryAfter = targetedProjection;
            modulePlan.projectedCargoUnitsAfter = totalCargoUnits(targetedProjection);
            continue;
        }

        const bool simulated = simulateModuleExecution(modulePlan, targetedProjection, plan.decisionLog);
        (void)simulated;
    }

    if (targetedProjection != reservationInventory) {
        appendLog(
            plan.decisionLog,
            "Planner simulation adjusted projected inventory to preserve per-module reservations."
        );
    }

    Inventory compressionReservationInventory = reservationInventory;
    planCompression(
        compressionReservationInventory,
        plan.compressionRefineSteps,
        plan.decisionLog,
        demandProfile
    );

    Inventory finalProjection = targetedProjection;
    simulateCompression(plan.compressionRefineSteps, finalProjection, plan.decisionLog);

    plan.projectedInventory = finalProjection;
    plan.projectedCargoUnits = totalCargoUnits(finalProjection);
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
        out << "Planner can free " << plan.freedCargoUnits << " cargo units after full simulation.";
        appendLog(plan.decisionLog, out.str());
    } else if (cargoFull && !settings.sellWhenBlocked) {
        appendLog(plan.decisionLog, "Cargo is full and sell is disabled; bot will remain blocked after planner work.");
    }

    return plan;
}

} // namespace dynamo
