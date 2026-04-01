#include "config.hpp"
#include <fstream>

namespace dynamo {

Config Config::load(const std::string& path) {
    Config cfg;
    std::ifstream file(path);
    if (file.is_open()) {
        nlohmann::json j;
        file >> j;
        
        if (j.contains("serverId")) cfg.serverId = j["serverId"];
        if (j.contains("username")) cfg.username = j["username"];
        if (j.contains("password")) cfg.password = j["password"];
        if (j.contains("clientInfo")) {
            auto& ci = j["clientInfo"];
            if (ci.contains("uid")) cfg.clientInfo.uid = ci["uid"];
            if (ci.contains("build")) cfg.clientInfo.build = ci["build"];
            if (ci.contains("version")) cfg.clientInfo.version = ci["version"].get<std::vector<int>>();
            if (ci.contains("platform")) cfg.clientInfo.platform = ci["platform"];
        }
    }
    return cfg;
}

void Config::save(const std::string& path) const {
    nlohmann::json j = {
        {"serverId", serverId},
        {"username", username},
        {"password", password},
        {"clientInfo", clientInfo.toJson()}
    };
    
    std::ofstream file(path);
    if (file.is_open()) {
        file << j.dump(2);
    }
}

} // namespace dynamo
