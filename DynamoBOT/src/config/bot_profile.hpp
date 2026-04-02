#pragma once

#include "bot/core/bot_config.hpp"
#include "bot/support/npc_database.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dynamo {

NLOHMANN_JSON_SERIALIZE_ENUM(BoxType, {
    {BoxType::BonusBox, "BonusBox"},
    {BoxType::CargoBox, "CargoBox"},
    {BoxType::EnergyBox, "EnergyBox"},
    {BoxType::GreenBox, "GreenBox"},
})

enum class NpcVariant {
    Default,
    Hyper,
    Ultra
};

NLOHMANN_JSON_SERIALIZE_ENUM(NpcVariant, {
    {NpcVariant::Default, "Default"},
    {NpcVariant::Hyper, "Hyper"},
    {NpcVariant::Ultra, "Ultra"},
})

struct NpcVariantRule {
    bool enabled{false};
    int32_t ammoType{1};
    int32_t rocketType{0};
    int32_t range{550};
    bool followOnLowHp{false};
    int32_t followOnLowHpPercent{25};
    bool ignoreOwnership{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    NpcVariantRule,
    enabled,
    ammoType,
    rocketType,
    range,
    followOnLowHp,
    followOnLowHpPercent,
    ignoreOwnership
)

struct NpcProfileRule {
    std::string npcName;
    NpcVariantRule defaultVariant;
    NpcVariantRule hyperVariant;
    NpcVariantRule ultraVariant;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(NpcProfileRule, npcName, defaultVariant, hyperVariant, ultraVariant)

struct ConfigSlotSelection {
    int32_t roaming{1};
    int32_t flying{2};
    int32_t shooting{1};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ConfigSlotSelection, roaming, flying, shooting)

struct SafetyPolicy {
    int32_t emergencyHpPercent{15};
    int32_t repairHpPercent{70};
    int32_t fullHpPercent{100};
    SafetyFleeMode fleeMode{SafetyFleeMode::OnEnemySeen};
};

inline void to_json(nlohmann::json& json, const SafetyPolicy& policy) {
    json = nlohmann::json{
        {"emergencyHpPercent", policy.emergencyHpPercent},
        {"repairHpPercent", policy.repairHpPercent},
        {"fullHpPercent", policy.fullHpPercent},
        {"fleeMode", policy.fleeMode},
    };
}

inline void from_json(const nlohmann::json& json, SafetyPolicy& policy) {
    policy = SafetyPolicy{};
    if (json.contains("emergencyHpPercent")) {
        json.at("emergencyHpPercent").get_to(policy.emergencyHpPercent);
    }
    if (json.contains("repairHpPercent")) {
        json.at("repairHpPercent").get_to(policy.repairHpPercent);
    }
    if (json.contains("fullHpPercent")) {
        json.at("fullHpPercent").get_to(policy.fullHpPercent);
    }
    if (json.contains("fleeMode")) {
        json.at("fleeMode").get_to(policy.fleeMode);
        return;
    }

    const bool runToSafetyIfSeeEnemy = json.value("runToSafetyIfSeeEnemy", false);
    const bool runToSafetyIfGetShot = json.value("runToSafetyIfGetShot", false);
    if (runToSafetyIfSeeEnemy) {
        policy.fleeMode = SafetyFleeMode::OnEnemySeen;
    } else if (runToSafetyIfGetShot) {
        policy.fleeMode = SafetyFleeMode::OnAttack;
    } else {
        policy.fleeMode = SafetyFleeMode::None;
    }
}

struct AdminDisconnectPolicy {
    bool enabled{true};
    int32_t cooldownMinutes{5};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AdminDisconnectPolicy, enabled, cooldownMinutes)

struct DeathDisconnectPolicy {
    bool enabled{false};
    int32_t deathThreshold{5};
    int32_t cooldownMinutes{15};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DeathDisconnectPolicy, enabled, deathThreshold, cooldownMinutes)

struct BotProfile {
    int32_t schemaVersion{2};
    std::string id{"default"};
    std::string displayName{"Default"};
    std::string workingMap{"R-1"};
    ConfigSlotSelection configSlots;
    bool kill{true};
    bool collect{true};
    bool collectDuringCombat{true};
    std::vector<BoxType> boxTypes;
    std::vector<std::string> avoidMaps;
    std::vector<NpcProfileRule> npcRules;
    SafetyPolicy safety;
    AdminDisconnectPolicy adminDisconnect;
    DeathDisconnectPolicy deathDisconnect;
    ResourceAutomationSettings resources;
    AutobuyConfig autobuy;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BotProfile, schemaVersion, id, displayName, workingMap, configSlots, kill, collect, collectDuringCombat, boxTypes, avoidMaps, npcRules, safety, adminDisconnect, deathDisconnect, resources, autobuy)

struct NpcRegistryEntry {
    std::string name;
    std::array<std::string, 3> namePatterns;
    std::vector<std::string> spawnMaps;
};

inline const std::array<NpcRegistryEntry, 12> kNpcRegistry = {{
    {"Hydro", {"-=(Hydro)=-", "-=(Hyper|Hydro)=-", "-=(Ultra|Hydro)=-"}, {"R-1", "R-2", "E-1", "E-2", "U-1", "U-2"}},
    {"Jenta", {"-=(Jenta)=-", "-=(Hyper|Jenta)=-", "-=(Ultra|Jenta)=-"}, {"R-2", "R-3", "E-2", "E-3", "U-2", "U-3"}},
    {"Mali", {"-=(Mali)=-", "-=(Hyper|Mali)=-", "-=(Ultra|Mali)=-"}, {"R-3", "R-4", "E-3", "E-4", "U-3", "U-4"}},
    {"Plarion", {"-=(Plarion)=-", "-=(Hyper|Plarion)=-", "-=(Ultra|Plarion)=-"}, {"R-3", "R-4", "E-3", "E-4", "U-3", "U-4"}},
    {"Motron", {"-=(Motron)=-", "-=(Hyper|Motron)=-", "-=(Ultra|Motron)=-"}, {"R-4", "R-5", "E-4", "E-5", "U-4", "U-5"}},
    {"Xeon", {"-=(Xeon)=-", "-=(Hyper|Xeon)=-", "-=(Ultra|Xeon)=-"}, {"R-4", "R-5", "E-4", "E-5", "U-4", "U-5"}},
    {"Bangoliour", {"-=(Bangoliour)=-", "-=(Hyper|Bangoliour)=-", "-=(Ultra|Bangoliour)=-"}, {"R-5", "R-6", "E-5", "E-6", "U-5", "U-6"}},
    {"Zavientos", {"-=(Zavientos)=-", "-=(Hyper|Zavientos)=-", "-=(Ultra|Zavientos)=-"}, {"R-6", "E-6", "U-6"}},
    {"Magmius", {"-=(Magmius)=-", "-=(Hyper|Magmius)=-", "-=(Ultra|Magmius)=-"}, {"R-6", "E-6", "U-6"}},
    {"Raider", {"-=(Raider)=-", "-=(Hyper|Raider)=-", "-=(Ultra|Raider)=-"}, {"R-7", "E-7", "U-7", "J-SO", "J-VO", "J-VS"}},
    {"Vortex", {"-=(Vortex)=-", "-=(Hyper|Vortex)=-", "-=(Ultra|Vortex)=-"}, {"J-SO", "J-VO", "J-VS"}},
    {"Quattroid", {"-=(Quattroid)=-", "-=(Hyper|Quattroid)=-", "-=(Ultra|Quattroid)=-"}, {"G-1"}},
}};

inline const NpcRegistryEntry* findNpcRegistryEntry(std::string_view npcName) {
    auto it = std::find_if(
        kNpcRegistry.begin(),
        kNpcRegistry.end(),
        [npcName](const NpcRegistryEntry& entry) { return entry.name == npcName; }
    );
    return it == kNpcRegistry.end() ? nullptr : &(*it);
}

inline std::vector<std::string> approvedNpcNames() {
    std::vector<std::string> names;
    names.reserve(kNpcRegistry.size());
    for (const auto& entry : kNpcRegistry) {
        names.push_back(entry.name);
    }
    return names;
}

inline NpcProfileRule defaultNpcRule(std::string npcName) {
    NpcProfileRule rule;
    rule.npcName = std::move(npcName);
    return rule;
}

inline BotProfile makeDefaultBotProfile(std::string id = "default",
                                        std::string displayName = "Default") {
    BotProfile profile;
    profile.id = std::move(id);
    profile.displayName = std::move(displayName);
    for (const auto& entry : kNpcRegistry) {
        profile.npcRules.push_back(defaultNpcRule(entry.name));
    }
    return profile;
}

} // namespace dynamo
