#pragma once

#include <string>
#include <optional>
#include <memory>
#include "core/config.hpp"

namespace dynamo {

class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    
    // Fetch server metadata from API
    std::optional<MetaInfo> fetchMetaInfo(const std::string& region = "eu");
    
    // Get login token
    std::optional<LoginToken> getLoginToken(
        const std::string& baseUrl,
        const std::string& username,
        const std::string& password
    );
    
    // Generic GET/POST methods
    std::optional<std::string> get(const std::string& url);
    std::optional<std::string> post(const std::string& url, const std::string& body, const std::string& contentType = "application/json");
    
    const std::string& lastError() const { return lastError_; }
    
private:
    std::string lastError_;
    
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dynamo
