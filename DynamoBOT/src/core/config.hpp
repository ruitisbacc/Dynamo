#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace dynamo {

struct GameServer {
    std::string id;
    std::string host;
    int port{0};
};

struct LoginServer {
    std::string gameServerId;
    std::string baseUrl;
};

struct MetaInfo {
    std::vector<GameServer> gameServers;
    std::vector<LoginServer> loginServers;
    std::string lastClientVersion;
    std::string loginServerUrl;  // Resolved login server base URL for current server
    
    std::optional<GameServer> getServer(const std::string& serverId) const {
        for (const auto& s : gameServers) {
            if (s.id == serverId) return s;
        }
        return std::nullopt;
    }
    
    std::optional<LoginServer> getLoginServer(const std::string& serverId) const {
        for (const auto& s : loginServers) {
            if (s.gameServerId == serverId) return s;
        }
        return std::nullopt;
    }
};

struct LoginToken {
    std::string tokenId;
    std::string token;
    
    std::string combined() const {
        return tokenId + ":" + token;
    }
};

struct ClientInfo {
    std::string uid;
    int build{0};                           // wupacket uses int 0, not string
    std::vector<int> version{1, 233, 0};
    std::string platform{"Desktop"};        // wupacket uses "Desktop"
    std::string systemLocale{"en_US"};      // wupacket uses "en_US"
    std::string preferredLocale{"en"};
    std::string clientHash{"269980fe6e943c59e8ff10338f719870"};
    
    nlohmann::json toJson() const {
        return {
            {"uid", uid},
            {"build", build},
            {"version", version},
            {"platform", platform},
            {"systemLocale", systemLocale},
            {"preferredLocale", preferredLocale},
            {"clientHash", clientHash}
        };
    }
};

struct Config {
    std::string serverId{"eu1"};
    std::string username;
    std::string password;
    ClientInfo clientInfo;
    
    static Config load(const std::string& path);
    void save(const std::string& path) const;
};

// JSON serialization
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GameServer, id, host, port)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LoginServer, gameServerId, baseUrl)

inline void from_json(const nlohmann::json& j, MetaInfo& m) {
    if (j.contains("gameServers")) {
        m.gameServers = j["gameServers"].get<std::vector<GameServer>>();
    }
    if (j.contains("loginServers")) {
        m.loginServers = j["loginServers"].get<std::vector<LoginServer>>();
    }
    if (j.contains("lastClientVersion")) {
        m.lastClientVersion = j["lastClientVersion"].get<std::string>();
    }
}

inline void from_json(const nlohmann::json& j, LoginToken& t) {
    j.at("tokenId").get_to(t.tokenId);
    j.at("token").get_to(t.token);
}

} // namespace dynamo
