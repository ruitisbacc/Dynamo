#pragma once

#include "bot_profile.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dynamo {

struct ResolvedBotConfig {
    uint64_t version{0};
    std::string sourceId;
    std::string sourceName;
    std::optional<std::filesystem::path> sourcePath;
    std::optional<BotProfile> profile;
    BotConfig runtime;
};

class ConfigService {
public:
    explicit ConfigService(std::filesystem::path profilesDirectory = defaultProfilesDirectory());

    [[nodiscard]] static std::filesystem::path defaultProfilesDirectory();

    bool loadProfiles(std::string* error = nullptr);
    bool saveProfile(BotProfile profile,
                     bool makeActive = false,
                     std::string* error = nullptr);
    bool setActiveProfile(const std::string& profileId, std::string* error = nullptr);

    [[nodiscard]] std::vector<BotProfile> profiles() const;
    [[nodiscard]] std::optional<BotProfile> activeProfile() const;
    [[nodiscard]] std::string activeProfileId() const;
    [[nodiscard]] std::shared_ptr<const ResolvedBotConfig> snapshot() const;
    [[nodiscard]] uint64_t version() const;

    void adoptLegacyConfig(BotConfig config,
                           std::string sourceId = "legacy",
                           std::string sourceName = "Legacy");
    bool setMode(BotMode mode, std::string* error = nullptr);
    bool setWorkingMap(const std::string& mapName, std::string* error = nullptr);

private:
    struct StoredProfile {
        BotProfile profile;
        std::filesystem::path path;
    };

    std::filesystem::path profilesDirectory_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, StoredProfile> profiles_;
    std::string activeProfileId_;
    uint64_t version_{0};
    std::shared_ptr<ResolvedBotConfig> activeSnapshot_;

    static std::string sanitizeProfileId(const std::string& rawId);
    static int32_t clampConfigSlot(int32_t slot);
    static std::string modeToSourceName(BotMode mode);
    static std::vector<std::string> validateProfile(const BotProfile& profile);
    static void normalizeProfile(BotProfile& profile);
    static BotMode deriveMode(const BotProfile& profile);
    static BotConfig resolveRuntimeConfig(const BotProfile& profile);
    static BotConfig normalizeLegacyRuntime(BotConfig runtime);

    bool setActiveProfileLocked(const std::string& profileId, std::string* error);
    bool rebuildActiveSnapshotLocked(const std::string& sourceId,
                                     const std::string& sourceName,
                                     const std::optional<std::filesystem::path>& sourcePath,
                                     const std::optional<BotProfile>& profile,
                                     BotConfig runtime);
};

} // namespace dynamo
