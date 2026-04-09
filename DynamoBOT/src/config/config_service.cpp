#include "config_service.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace dynamo {

namespace {

constexpr int32_t kDefaultCombatPriority = 60;
constexpr int32_t kDefaultCollectPriority = 40;
constexpr int32_t kDefaultRoamingPriority = 10;
constexpr int32_t kDefaultSafetyPriority = 90;
constexpr int32_t kFixedProfileTickRateMs = 16;
constexpr int32_t kFixedAdminDroneThreshold = 8;
constexpr int32_t kMinNpcRange = 200;
constexpr int32_t kMaxNpcRange = 800;
constexpr int32_t kMinLowHpFollowPercent = 1;
constexpr int32_t kMaxLowHpFollowPercent = 100;
constexpr int32_t kMinResourcePriority = 1;
constexpr int32_t kMaxResourcePriority = 4;
constexpr int32_t kMinRefineIntervalSeconds = 30;
constexpr int32_t kMaxRefineIntervalSeconds = 600;

int32_t boxPriority(BoxType type) {
    switch (type) {
        case BoxType::GreenBox: return 40;
        case BoxType::BonusBox: return 20;
        case BoxType::CargoBox: return 10;
        default: return 0;
    }
}

bool containsBoxType(const std::vector<BoxType>& values, BoxType type) {
    return std::find(values.begin(), values.end(), type) != values.end();
}

const char* resourceModuleLabel(ResourceModuleType moduleType) {
    switch (moduleType) {
        case ResourceModuleType::Lasers: return "lasers";
        case ResourceModuleType::Rockets: return "rockets";
        case ResourceModuleType::Shields: return "shields";
        case ResourceModuleType::Speed: return "speed";
        default: return "resource module";
    }
}

void normalizeResourceModuleSettings(ResourceModuleSettings& settings, ResourceModuleType moduleType) {
    settings.priority = std::clamp(settings.priority, kMinResourcePriority, kMaxResourcePriority);
    if (!isAllowedEnrichmentMaterial(moduleType, settings.material)) {
        settings.material = defaultResourceMaterial(moduleType);
    }
}

void normalizeResourceAutomationSettings(ResourceAutomationSettings& resources) {
    resources.refineIntervalSeconds = std::clamp(resources.refineIntervalSeconds,
                                                  kMinRefineIntervalSeconds,
                                                  kMaxRefineIntervalSeconds);
    normalizeResourceModuleSettings(resources.speed, ResourceModuleType::Speed);
    normalizeResourceModuleSettings(resources.shields, ResourceModuleType::Shields);
    normalizeResourceModuleSettings(resources.lasers, ResourceModuleType::Lasers);
    normalizeResourceModuleSettings(resources.rockets, ResourceModuleType::Rockets);

    struct PriorityEntry {
        ResourceModuleSettings* settings{nullptr};
        int32_t fallbackPriority{0};
    };

    std::array<PriorityEntry, 4> ordered = {{
        {&resources.speed, defaultResourcePriority(ResourceModuleType::Speed)},
        {&resources.shields, defaultResourcePriority(ResourceModuleType::Shields)},
        {&resources.lasers, defaultResourcePriority(ResourceModuleType::Lasers)},
        {&resources.rockets, defaultResourcePriority(ResourceModuleType::Rockets)},
    }};

    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const PriorityEntry& lhs, const PriorityEntry& rhs) {
            if (lhs.settings->priority != rhs.settings->priority) {
                return lhs.settings->priority < rhs.settings->priority;
            }
            return lhs.fallbackPriority < rhs.fallbackPriority;
        }
    );

    int32_t normalizedPriority = kMinResourcePriority;
    for (auto& entry : ordered) {
        entry.settings->priority = normalizedPriority++;
    }
}

void validateResourceModuleSettings(const ResourceModuleSettings& settings,
                                    ResourceModuleType moduleType,
                                    std::vector<std::string>& errors) {
    if (settings.priority < kMinResourcePriority || settings.priority > kMaxResourcePriority) {
        errors.emplace_back(
            std::string(resourceModuleLabel(moduleType)) + " priority must be in range 1-4"
        );
    }
    if (!isAllowedEnrichmentMaterial(moduleType, settings.material)) {
        errors.emplace_back(
            std::string("Invalid material for ") + resourceModuleLabel(moduleType)
        );
    }
}

std::string joinErrors(const std::vector<std::string>& errors) {
    std::ostringstream buffer;
    for (std::size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) {
            buffer << "; ";
        }
        buffer << errors[i];
    }
    return buffer.str();
}

NpcTargetConfig makeRuntimeTarget(const std::string& pattern,
                                  const NpcVariantRule& rule) {
    NpcTargetConfig target;
    target.namePattern = pattern;
    target.priority = 1;
    target.ammoType = rule.ammoType;
    target.rocketType = rule.rocketType;
    target.range = std::clamp(rule.range, kMinNpcRange, kMaxNpcRange);
    target.followOnLowHp = rule.followOnLowHp;
    target.followOnLowHpPercent = std::clamp(
        rule.followOnLowHpPercent,
        kMinLowHpFollowPercent,
        kMaxLowHpFollowPercent
    );
    target.ignoreOwnership = rule.ignoreOwnership;
    target.maxDistance = 0;
    return target;
}

std::optional<std::filesystem::path> executableDirectory() {
#ifdef _WIN32
    std::wstring buffer(512, L'\0');
    while (true) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::nullopt;
        }

        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }

        buffer.resize(buffer.size() * 2);
    }
#else
    return std::nullopt;
#endif
}

} // namespace

ConfigService::ConfigService(std::filesystem::path profilesDirectory)
    : profilesDirectory_(std::move(profilesDirectory)) {
    auto defaultProfile = makeDefaultBotProfile();
    StoredProfile stored{defaultProfile, {}};
    profiles_.emplace(defaultProfile.id, std::move(stored));
    setActiveProfileLocked(defaultProfile.id, nullptr);
    loadProfiles(nullptr);
}

std::filesystem::path ConfigService::defaultProfilesDirectory() {
    const auto cwd = std::filesystem::current_path();
    std::vector<std::filesystem::path> candidates;
    candidates.reserve(8);

    if (const auto exeDir = executableDirectory()) {
        candidates.push_back(*exeDir / "config" / "profiles");
        candidates.push_back(exeDir->parent_path() / "config" / "profiles");
        candidates.push_back(exeDir->parent_path() / "DynamoBOT" / "config" / "profiles");
    }

    const std::array<std::filesystem::path, 5> cwdCandidates = {
        cwd / "config" / "profiles",
        cwd / "DynamoBOT" / "config" / "profiles",
        cwd.parent_path() / "config" / "profiles",
        cwd.parent_path().parent_path() / "config" / "profiles",
        cwd.parent_path().parent_path() / "DynamoBOT" / "config" / "profiles",
    };
    candidates.insert(candidates.end(), cwdCandidates.begin(), cwdCandidates.end());

    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
            return candidate;
        }
    }

    if (const auto exeDir = executableDirectory()) {
        return *exeDir / "config" / "profiles";
    }

    return cwd / "config" / "profiles";
}

bool ConfigService::loadProfiles(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    profiles_.clear();
    activeProfileId_.clear();

    std::error_code ec;
    if (!std::filesystem::exists(profilesDirectory_, ec)) {
        std::filesystem::create_directories(profilesDirectory_, ec);
    }
    if (ec) {
        if (error) {
            *error = "Unable to prepare profiles directory: " + ec.message();
        }
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(profilesDirectory_, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file.is_open()) {
            continue;
        }

        try {
            nlohmann::json json;
            file >> json;

            BotProfile profile = json.get<BotProfile>();
            if (profile.id.empty()) {
                profile.id = entry.path().stem().string();
            }
            normalizeProfile(profile);

            const auto errors = validateProfile(profile);
            if (!errors.empty()) {
                if (error && error->empty()) {
                    *error = entry.path().filename().string() + ": " + joinErrors(errors);
                }
                continue;
            }

            profiles_[profile.id] = StoredProfile{profile, entry.path()};
        } catch (const std::exception& ex) {
            if (error && error->empty()) {
                *error = entry.path().filename().string() + ": " + ex.what();
            }
        }
    }

    if (profiles_.empty()) {
        auto defaultProfile = makeDefaultBotProfile();
        profiles_.emplace(defaultProfile.id, StoredProfile{defaultProfile, {}});
    }

    if (profiles_.contains("default")) {
        return setActiveProfileLocked("default", error);
    }

    std::vector<std::string> ids;
    ids.reserve(profiles_.size());
    for (const auto& [id, _] : profiles_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return setActiveProfileLocked(ids.front(), error);
}

bool ConfigService::saveProfile(BotProfile profile, bool makeActive, std::string* error) {
    normalizeProfile(profile);
    const auto errors = validateProfile(profile);
    if (!errors.empty()) {
        if (error) {
            *error = joinErrors(errors);
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(profilesDirectory_, ec);
    if (ec) {
        if (error) {
            *error = "Unable to create profiles directory: " + ec.message();
        }
        return false;
    }

    const auto path = profilesDirectory_ / (profile.id + ".json");
    std::ofstream file(path);
    if (!file.is_open()) {
        if (error) {
            *error = "Unable to write profile file: " + path.string();
        }
        return false;
    }

    nlohmann::json json = profile;
    file << json.dump(2);
    file.close();

    std::lock_guard<std::mutex> lock(mutex_);
    profiles_[profile.id] = StoredProfile{profile, path};
    if (makeActive || activeProfileId_.empty() || activeProfileId_ == profile.id) {
        return setActiveProfileLocked(profile.id, error);
    }
    return true;
}

bool ConfigService::setActiveProfile(const std::string& profileId, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    return setActiveProfileLocked(profileId, error);
}

std::vector<BotProfile> ConfigService::profiles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BotProfile> result;
    result.reserve(profiles_.size());
    for (const auto& [_, stored] : profiles_) {
        result.push_back(stored.profile);
    }
    std::sort(result.begin(), result.end(), [](const BotProfile& lhs, const BotProfile& rhs) {
        return lhs.displayName < rhs.displayName;
    });
    return result;
}

std::optional<BotProfile> ConfigService::activeProfile() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = profiles_.find(activeProfileId_);
    if (it == profiles_.end()) {
        return std::nullopt;
    }
    return it->second.profile;
}

std::string ConfigService::activeProfileId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeProfileId_;
}

std::shared_ptr<const ResolvedBotConfig> ConfigService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeSnapshot_;
}

uint64_t ConfigService::version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return version_;
}

void ConfigService::adoptLegacyConfig(BotConfig config,
                                      std::string sourceId,
                                      std::string sourceName) {
    std::lock_guard<std::mutex> lock(mutex_);
    activeProfileId_.clear();
    rebuildActiveSnapshotLocked(
        sanitizeProfileId(sourceId),
        sourceName.empty() ? "Legacy" : sourceName,
        std::nullopt,
        std::nullopt,
        normalizeLegacyRuntime(std::move(config))
    );
}

bool ConfigService::setMode(BotMode mode, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = profiles_.find(activeProfileId_);
    if (it != profiles_.end()) {
        switch (mode) {
            case BotMode::Kill:
                it->second.profile.kill = true;
                it->second.profile.collect = false;
                break;
            case BotMode::Collect:
                it->second.profile.kill = false;
                it->second.profile.collect = true;
                break;
            case BotMode::KillCollect:
                it->second.profile.kill = true;
                it->second.profile.collect = true;
                break;
        }
        return setActiveProfileLocked(activeProfileId_, error);
    }

    if (!activeSnapshot_) {
        if (error) {
            *error = "No active config snapshot";
        }
        return false;
    }

    auto runtime = activeSnapshot_->runtime;
    runtime.mode = mode;
    if (mode == BotMode::Kill) {
        runtime.collect.enabled = false;
    } else if (mode == BotMode::Collect) {
        runtime.combat.enabled = false;
    } else {
        runtime.combat.enabled = true;
        runtime.collect.enabled = true;
    }
    return rebuildActiveSnapshotLocked(
        activeSnapshot_->sourceId,
        modeToSourceName(mode),
        activeSnapshot_->sourcePath,
        std::nullopt,
        normalizeLegacyRuntime(std::move(runtime))
    );
}

bool ConfigService::setWorkingMap(const std::string& mapName, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = profiles_.find(activeProfileId_);
    if (it != profiles_.end()) {
        it->second.profile.workingMap = mapName;
        return setActiveProfileLocked(activeProfileId_, error);
    }

    if (!activeSnapshot_) {
        if (error) {
            *error = "No active config snapshot";
        }
        return false;
    }

    auto runtime = activeSnapshot_->runtime;
    runtime.map.workingMap = mapName;
    return rebuildActiveSnapshotLocked(
        activeSnapshot_->sourceId,
        activeSnapshot_->sourceName,
        activeSnapshot_->sourcePath,
        std::nullopt,
        normalizeLegacyRuntime(std::move(runtime))
    );
}

std::string ConfigService::sanitizeProfileId(const std::string& rawId) {
    std::string result;
    result.reserve(rawId.size());
    for (const char ch : rawId) {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        } else if (ch == '-' || ch == '_') {
            result.push_back(ch);
        } else if (ch == ' ' || ch == '.') {
            result.push_back('-');
        }
    }
    return result.empty() ? "profile" : result;
}

int32_t ConfigService::clampConfigSlot(int32_t slot) {
    return slot == 2 ? 2 : 1;
}

std::string ConfigService::modeToSourceName(BotMode mode) {
    switch (mode) {
        case BotMode::Kill: return "Legacy Kill";
        case BotMode::Collect: return "Legacy Collect";
        case BotMode::KillCollect: return "Legacy KillCollect";
        default: return "Legacy";
    }
}

std::vector<std::string> ConfigService::validateProfile(const BotProfile& profile) {
    std::vector<std::string> errors;

    if (profile.id.empty()) {
        errors.emplace_back("Profile id must not be empty");
    }
    if (profile.displayName.empty()) {
        errors.emplace_back("Profile displayName must not be empty");
    }
    if (!profile.kill && !profile.collect) {
        errors.emplace_back("At least one of kill or collect must be enabled");
    }
    if (profile.workingMap.empty()) {
        errors.emplace_back("workingMap must not be empty");
    }
    if (profile.configSlots.roaming < 1 || profile.configSlots.roaming > 2) {
        errors.emplace_back("roaming config slot must be 1 or 2");
    }
    if (profile.configSlots.flying < 1 || profile.configSlots.flying > 2) {
        errors.emplace_back("flying config slot must be 1 or 2");
    }
    if (profile.configSlots.shooting < 1 || profile.configSlots.shooting > 2) {
        errors.emplace_back("shooting config slot must be 1 or 2");
    }
    if (profile.configSlots.collect < 1 || profile.configSlots.collect > 2) {
        errors.emplace_back("collect config slot must be 1 or 2");
    }
    if (profile.safety.emergencyHpPercent < 1 || profile.safety.emergencyHpPercent > 100) {
        errors.emplace_back("emergencyHpPercent must be in range 1-100");
    }
    if (profile.safety.repairHpPercent < 1 || profile.safety.repairHpPercent > 100) {
        errors.emplace_back("repairHpPercent must be in range 1-100");
    }
    if (profile.safety.emergencyHpPercent > profile.safety.repairHpPercent) {
        errors.emplace_back("emergencyHpPercent must be <= repairHpPercent");
    }
    if (profile.safety.fullHpPercent < profile.safety.repairHpPercent ||
        profile.safety.fullHpPercent > 100) {
        errors.emplace_back("fullHpPercent must be >= repairHpPercent and <= 100");
    }
    if (profile.adminDisconnect.cooldownMinutes < 0) {
        errors.emplace_back("admin disconnect cooldown must not be negative");
    }
    if (profile.deathDisconnect.enabled && profile.deathDisconnect.deathThreshold <= 0) {
        errors.emplace_back("deathThreshold must be > 0 when death disconnect is enabled");
    }
    if (profile.deathDisconnect.cooldownMinutes < 0) {
        errors.emplace_back("death disconnect cooldown must not be negative");
    }
    validateResourceModuleSettings(profile.resources.speed, ResourceModuleType::Speed, errors);
    validateResourceModuleSettings(profile.resources.shields, ResourceModuleType::Shields, errors);
    validateResourceModuleSettings(profile.resources.lasers, ResourceModuleType::Lasers, errors);
    validateResourceModuleSettings(profile.resources.rockets, ResourceModuleType::Rockets, errors);

    std::vector<std::string> npcNames;
    npcNames.reserve(profile.npcRules.size());
    for (const auto& rule : profile.npcRules) {
        if (!findNpcRegistryEntry(rule.npcName)) {
            errors.emplace_back("Unknown NPC in npcRules: " + rule.npcName);
            continue;
        }
        if (std::find(npcNames.begin(), npcNames.end(), rule.npcName) != npcNames.end()) {
            errors.emplace_back("Duplicate NPC rule: " + rule.npcName);
        }
        npcNames.push_back(rule.npcName);

        const auto validateVariant = [&errors, &rule](const char* label, const NpcVariantRule& variant) {
            if (variant.ammoType < 1 || variant.ammoType > 6) {
                errors.emplace_back(
                    "Invalid laser ammo for " + rule.npcName + " " + label + " variant"
                );
            }
            if (variant.rocketType < 0 || variant.rocketType > 3) {
                errors.emplace_back(
                    "Invalid rocket ammo for " + rule.npcName + " " + label + " variant"
                );
            }
            if (variant.range < kMinNpcRange || variant.range > kMaxNpcRange) {
                errors.emplace_back(
                    "Invalid range for " + rule.npcName + " " + label + " variant"
                );
            }
            if (variant.followOnLowHpPercent < kMinLowHpFollowPercent ||
                variant.followOnLowHpPercent > kMaxLowHpFollowPercent) {
                errors.emplace_back(
                    "Invalid low HP follow threshold for " + rule.npcName + " " + label + " variant"
                );
            }
        };

        validateVariant("Default", rule.defaultVariant);
        validateVariant("Hyper", rule.hyperVariant);
        validateVariant("Ultra", rule.ultraVariant);
    }

    return errors;
}

void ConfigService::normalizeProfile(BotProfile& profile) {
    const auto normalizeVariant = [](NpcVariantRule& variant) {
        variant.ammoType = std::clamp(variant.ammoType, 1, 6);
        variant.rocketType = std::clamp(variant.rocketType, 0, 3);
        variant.range = std::clamp(variant.range, kMinNpcRange, kMaxNpcRange);
        variant.followOnLowHpPercent = std::clamp(
            variant.followOnLowHpPercent,
            kMinLowHpFollowPercent,
            kMaxLowHpFollowPercent
        );
    };

    profile.schemaVersion = std::max(profile.schemaVersion, 2);
    profile.id = sanitizeProfileId(profile.id.empty() ? profile.displayName : profile.id);
    if (profile.displayName.empty()) {
        profile.displayName = profile.id;
    }

    profile.configSlots.roaming = clampConfigSlot(profile.configSlots.roaming);
    profile.configSlots.flying = clampConfigSlot(profile.configSlots.flying);
    profile.configSlots.shooting = clampConfigSlot(profile.configSlots.shooting);
    profile.configSlots.collect = clampConfigSlot(profile.configSlots.collect);
    profile.collectDuringCombat = profile.collectDuringCombat && profile.kill && profile.collect;
    profile.safety.emergencyHpPercent = std::clamp(profile.safety.emergencyHpPercent, 1, 100);
    profile.safety.repairHpPercent = std::clamp(profile.safety.repairHpPercent, 1, 100);
    profile.safety.emergencyHpPercent = std::min(
        profile.safety.emergencyHpPercent,
        profile.safety.repairHpPercent
    );
    profile.safety.fullHpPercent = std::clamp(
        profile.safety.fullHpPercent,
        profile.safety.repairHpPercent,
        100
    );
    switch (profile.safety.fleeMode) {
        case SafetyFleeMode::None:
        case SafetyFleeMode::OnAttack:
        case SafetyFleeMode::OnEnemySeen:
            break;
        default:
            profile.safety.fleeMode = SafetyFleeMode::OnEnemySeen;
            break;
    }
    profile.adminDisconnect.cooldownMinutes = std::max(0, profile.adminDisconnect.cooldownMinutes);
    profile.deathDisconnect.deathThreshold = std::max(1, profile.deathDisconnect.deathThreshold);
    profile.deathDisconnect.cooldownMinutes = std::max(0, profile.deathDisconnect.cooldownMinutes);
    normalizeResourceAutomationSettings(profile.resources);

    std::vector<BoxType> dedupedBoxes;
    dedupedBoxes.reserve(profile.boxTypes.size());
    for (const auto type : profile.boxTypes) {
        if (type == BoxType::EnergyBox) {
            continue;
        }
        if (!containsBoxType(dedupedBoxes, type)) {
            dedupedBoxes.push_back(type);
        }
    }
    profile.boxTypes = std::move(dedupedBoxes);

    std::vector<NpcProfileRule> normalizedRules;
    normalizedRules.reserve(kNpcRegistry.size());
    for (const auto& entry : kNpcRegistry) {
        auto it = std::find_if(
            profile.npcRules.begin(),
            profile.npcRules.end(),
            [&entry](const NpcProfileRule& rule) { return rule.npcName == entry.name; }
        );
        if (it != profile.npcRules.end()) {
            it->npcName = entry.name;
            normalizeVariant(it->defaultVariant);
            normalizeVariant(it->hyperVariant);
            normalizeVariant(it->ultraVariant);
            normalizedRules.push_back(*it);
        } else {
            normalizedRules.push_back(defaultNpcRule(entry.name));
        }
    }
    profile.npcRules = std::move(normalizedRules);
}

BotMode ConfigService::deriveMode(const BotProfile& profile) {
    if (profile.kill && profile.collect) {
        return BotMode::KillCollect;
    }
    return profile.kill ? BotMode::Kill : BotMode::Collect;
}

BotConfig ConfigService::resolveRuntimeConfig(const BotProfile& profile) {
    BotConfig runtime;
    runtime.mode = deriveMode(profile);
    runtime.tickRateMs = kFixedProfileTickRateMs;

    runtime.map.enabled = true;
    runtime.map.workingMap = profile.workingMap;
    runtime.map.workingMapId = 0;
    runtime.map.autoTravelToWorkingMap = true;
    runtime.map.travelConfigId = profile.configSlots.flying;
    runtime.map.avoidMaps = profile.avoidMaps;

    runtime.roaming.enabled = true;
    runtime.roaming.moveCooldownMs = 180;
    runtime.roaming.mapMargin = 500;
    runtime.roaming.configId = profile.configSlots.roaming;
    runtime.roaming.priority = kDefaultRoamingPriority;

    runtime.collect.enabled = profile.collect;
    runtime.collect.maxCollectDistance = 1500;
    runtime.collect.collectCooldownMs = 500;
    runtime.collect.moveCooldownMs = 200;
    runtime.collect.skipBootyIfNoKeys = true;
    runtime.collect.skipResourceIfCargoFull = true;
    runtime.collect.collectDuringCombat = profile.collectDuringCombat && runtime.mode == BotMode::KillCollect;
    runtime.collect.combatCollectMaxDistance = 800;
    runtime.collect.configId = profile.configSlots.collect;
    runtime.collect.priority = kDefaultCollectPriority;
    runtime.collect.targetBoxes.clear();
    runtime.collect.targetBoxes.reserve(profile.boxTypes.size());
    for (const auto type : profile.boxTypes) {
        runtime.collect.targetBoxes.push_back(
            BoxTargetConfig{
                static_cast<int32_t>(type),
                boxPriority(type),
                true
            }
        );
    }

    runtime.combat.enabled = profile.kill;
    runtime.combat.targetEngagedNpc = false;
    runtime.combat.maxCombatDistance = 2000;
    runtime.combat.attackRange = 600;
    runtime.combat.selectCooldownMs = 250;
    runtime.combat.attackCooldownMs = 200;
    runtime.combat.useRockets = false;
    runtime.combat.rocketCooldownMs = 2000;
    runtime.combat.followDistance = 500;
    runtime.combat.moveCooldownMs = 200;
    runtime.combat.configId = profile.configSlots.shooting;
    runtime.combat.randomMovement = true;
    runtime.combat.antibanMoveIntervalMs = 20000;
    runtime.combat.priority = kDefaultCombatPriority;
    runtime.combat.targets.clear();
    for (const auto& rule : profile.npcRules) {
        const auto* entry = findNpcRegistryEntry(rule.npcName);
        if (!entry) {
            continue;
        }
        if (rule.defaultVariant.enabled) {
            runtime.combat.targets.push_back(
                makeRuntimeTarget(entry->namePatterns[0], rule.defaultVariant)
            );
        }
        if (rule.hyperVariant.enabled) {
            runtime.combat.targets.push_back(
                makeRuntimeTarget(entry->namePatterns[1], rule.hyperVariant)
            );
        }
        if (rule.ultraVariant.enabled) {
            runtime.combat.targets.push_back(
                makeRuntimeTarget(entry->namePatterns[2], rule.ultraVariant)
            );
        }
        runtime.combat.useRockets =
            runtime.combat.useRockets ||
            rule.defaultVariant.rocketType > 0 ||
            rule.hyperVariant.rocketType > 0 ||
            rule.ultraVariant.rocketType > 0;
    }

    runtime.safety.enabled = true;
    runtime.safety.minHpPercent = profile.safety.emergencyHpPercent;
    runtime.safety.repairHpPercent = profile.safety.repairHpPercent;
    runtime.safety.fullHpPercent = profile.safety.fullHpPercent;
    runtime.safety.fleeMode = profile.safety.fleeMode;
    runtime.safety.enemySeenTimeoutMs = 15000;
    runtime.safety.preferPortalEscape = true;
    runtime.safety.fleeMoveCooldownMs = 200;
    runtime.safety.adminEscapeDelayMs = profile.adminDisconnect.cooldownMinutes * 60 * 1000;
    runtime.safety.useEscapeConfig = true;
    runtime.safety.escapeConfigId = profile.configSlots.flying;
    runtime.safety.fightConfigId = profile.configSlots.shooting;
    runtime.safety.configSwitchCooldownMs = 1000;
    runtime.safety.priority = kDefaultSafetyPriority;

    runtime.revive.enabled = true;
    runtime.revive.waitBeforeReviveMs = 250;
    runtime.revive.waitAfterReviveMs = 5000;
    runtime.revive.maxDeaths = profile.deathDisconnect.enabled ? profile.deathDisconnect.deathThreshold : 0;
    runtime.revive.stopBotOnMaxDeaths = false;
    runtime.revive.disconnectOnMaxDeaths = profile.deathDisconnect.enabled;
    runtime.revive.disconnectCooldownMinutes = profile.deathDisconnect.cooldownMinutes;

    runtime.admin.enabled = profile.adminDisconnect.enabled;
    runtime.admin.droneCountThreshold = kFixedAdminDroneThreshold;
    runtime.admin.escapeDelayMs = profile.adminDisconnect.cooldownMinutes * 60 * 1000;
    runtime.admin.disconnectWhenSeen = profile.adminDisconnect.enabled;
    runtime.admin.disconnectCooldownMinutes = profile.adminDisconnect.cooldownMinutes;

    runtime.resources = profile.resources;
    runtime.autobuy = profile.autobuy;

    return normalizeLegacyRuntime(std::move(runtime));
}

BotConfig ConfigService::normalizeLegacyRuntime(BotConfig runtime) {
    runtime.tickRateMs = kFixedProfileTickRateMs;

    runtime.combat.configId = clampConfigSlot(runtime.combat.configId);
    runtime.collect.configId = clampConfigSlot(runtime.collect.configId);
    runtime.roaming.configId = clampConfigSlot(runtime.roaming.configId);
    runtime.map.travelConfigId = clampConfigSlot(runtime.map.travelConfigId);
    runtime.safety.escapeConfigId = clampConfigSlot(runtime.safety.escapeConfigId);
    runtime.safety.fightConfigId = clampConfigSlot(runtime.safety.fightConfigId);
    runtime.admin.disconnectCooldownMinutes = std::max(0, runtime.admin.disconnectCooldownMinutes);
    runtime.revive.disconnectCooldownMinutes = std::max(0, runtime.revive.disconnectCooldownMinutes);
    runtime.revive.maxDeaths = std::max(0, runtime.revive.maxDeaths);
    runtime.admin.droneCountThreshold = kFixedAdminDroneThreshold;
    runtime.collect.collectDuringCombat =
        runtime.collect.collectDuringCombat && runtime.mode == BotMode::KillCollect;
    normalizeResourceAutomationSettings(runtime.resources);
    switch (runtime.safety.fleeMode) {
        case SafetyFleeMode::None:
        case SafetyFleeMode::OnAttack:
        case SafetyFleeMode::OnEnemySeen:
            break;
        default:
            runtime.safety.fleeMode = SafetyFleeMode::OnEnemySeen;
            break;
    }

    return runtime;
}

bool ConfigService::setActiveProfileLocked(const std::string& profileId, std::string* error) {
    auto it = profiles_.find(profileId);
    if (it == profiles_.end()) {
        if (error) {
            *error = "Unknown profile id: " + profileId;
        }
        return false;
    }

    activeProfileId_ = profileId;
    return rebuildActiveSnapshotLocked(
        it->second.profile.id,
        it->second.profile.displayName,
        it->second.path.empty() ? std::optional<std::filesystem::path>{} : std::make_optional(it->second.path),
        it->second.profile,
        resolveRuntimeConfig(it->second.profile)
    );
}

bool ConfigService::rebuildActiveSnapshotLocked(const std::string& sourceId,
                                                const std::string& sourceName,
                                                const std::optional<std::filesystem::path>& sourcePath,
                                                const std::optional<BotProfile>& profile,
                                                BotConfig runtime) {
    auto snapshot = std::make_shared<ResolvedBotConfig>();
    snapshot->version = ++version_;
    snapshot->sourceId = sourceId;
    snapshot->sourceName = sourceName;
    snapshot->sourcePath = sourcePath;
    snapshot->profile = profile;
    snapshot->runtime = std::move(runtime);
    activeSnapshot_ = std::move(snapshot);
    return true;
}

} // namespace dynamo
