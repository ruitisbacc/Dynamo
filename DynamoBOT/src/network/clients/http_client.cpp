#include "http_client.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <format>
#include <algorithm>
#include <cctype>
#include <string_view>
#include <regex>

namespace dynamo {

struct HttpClient::Impl {
    // Using cpp-httplib for HTTP requests
};

namespace {

std::string trimCopy(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

std::string stripUtf8Bom(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

std::string responseSnippet(std::string_view body, std::size_t maxLen = 180) {
    std::string out;
    out.reserve((std::min)(body.size(), maxLen));
    for (char c : body) {
        if (out.size() >= maxLen) {
            break;
        }
        out.push_back((c == '\r' || c == '\n') ? ' ' : c);
    }
    return out;
}

struct ParsedUrl {
    bool https{false};
    std::string host;
    int port{80};
    std::string path{"/"};
};

struct HttpResponse {
    std::optional<std::string> body;
    std::optional<httplib::Error> error;
    int status{0};
    std::string detail;
};

std::string toLowerCopy(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (const unsigned char ch : input) {
        output.push_back(static_cast<char>(std::tolower(ch)));
    }
    return output;
}

bool endsWithInsensitive(std::string_view value, std::string_view suffix) {
    if (suffix.size() > value.size()) {
        return false;
    }
    return toLowerCopy(value.substr(value.size() - suffix.size())) == toLowerCopy(suffix);
}

bool isTrustedWarUniverseHost(std::string_view host) {
    const auto lowerHost = toLowerCopy(host);
    return lowerHost == "waruniverse.space" ||
           lowerHost == "waruniverse.com" ||
           endsWithInsensitive(lowerHost, ".waruniverse.space") ||
           endsWithInsensitive(lowerHost, ".waruniverse.com");
}

bool parseUrl(const std::string& url, ParsedUrl& parsed, std::string& error) {
    const std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        error = "Invalid URL: no scheme";
        return false;
    }

    const std::string scheme = toLowerCopy(std::string_view(url).substr(0, schemeEnd));
    if (scheme != "http" && scheme != "https") {
        error = "Invalid URL: unsupported scheme";
        return false;
    }

    parsed.https = scheme == "https";
    parsed.port = parsed.https ? 443 : 80;

    const std::size_t hostStart = schemeEnd + 3;
    const std::size_t pathStart = url.find('/', hostStart);
    std::string host = pathStart != std::string::npos
        ? url.substr(hostStart, pathStart - hostStart)
        : url.substr(hostStart);

    if (host.empty()) {
        error = "Invalid URL: empty host";
        return false;
    }

    parsed.path = pathStart != std::string::npos ? url.substr(pathStart) : "/";

    const std::size_t colonPos = host.rfind(':');
    if (colonPos != std::string::npos) {
        try {
            parsed.port = std::stoi(host.substr(colonPos + 1));
        } catch (const std::exception&) {
            error = "Invalid URL: bad port";
            return false;
        }
        host = host.substr(0, colonPos);
    }

    parsed.host = host;
    return true;
}

template <typename TClient>
void configureClient(TClient& cli) {
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);
    cli.set_follow_location(true);
}

HttpResponse finalizeResult(const httplib::Result& res) {
    HttpResponse response;
    if (!res) {
        response.error = res.error();
        response.detail = std::format("HTTP request failed: {}", httplib::to_string(res.error()));
        return response;
    }

    response.status = res->status;
    if (res->status < 200 || res->status >= 300) {
        response.detail = std::format("HTTP error: {}", res->status);
        return response;
    }

    response.body = res->body;
    return response;
}

HttpResponse performGetRequest(const ParsedUrl& parsed, bool verifyCertificates) {
    if (parsed.https) {
        httplib::SSLClient cli(parsed.host, parsed.port);
        configureClient(cli);
        cli.enable_server_certificate_verification(verifyCertificates);
        return finalizeResult(cli.Get(parsed.path));
    }

    httplib::Client cli(parsed.host, parsed.port);
    configureClient(cli);
    return finalizeResult(cli.Get(parsed.path));
}

HttpResponse performPostRequest(const ParsedUrl& parsed,
                                const std::string& body,
                                const std::string& contentType,
                                bool verifyCertificates) {
    if (parsed.https) {
        httplib::SSLClient cli(parsed.host, parsed.port);
        configureClient(cli);
        cli.enable_server_certificate_verification(verifyCertificates);
        return finalizeResult(cli.Post(parsed.path, body, contentType));
    }

    httplib::Client cli(parsed.host, parsed.port);
    configureClient(cli);
    return finalizeResult(cli.Post(parsed.path, body, contentType));
}

std::string urlEncode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);

    for (unsigned char ch : value) {
        const bool unreserved =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (unreserved) {
            encoded.push_back(static_cast<char>(ch));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[(ch >> 4) & 0x0F]);
            encoded.push_back(kHex[ch & 0x0F]);
        }
    }

    return encoded;
}

bool parseCombinedToken(const std::string& value, LoginToken& out) {
    const std::size_t sep = value.find(':');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= value.size()) {
        return false;
    }
    out.tokenId = value.substr(0, sep);
    out.token = value.substr(sep + 1);
    return !out.tokenId.empty() && !out.token.empty();
}

std::optional<std::string> getStringByKeys(
    const nlohmann::json& node,
    std::initializer_list<const char*> keys) {
    if (!node.is_object()) {
        return std::nullopt;
    }
    for (const char* key : keys) {
        if (!node.contains(key)) {
            continue;
        }
        const auto& value = node.at(key);
        if (value.is_string()) {
            return value.get<std::string>();
        } else if (value.is_number_integer()) {
            return std::to_string(value.get<long long>());
        } else if (value.is_number_float()) {
            return std::to_string(value.get<double>());
        } else if (value.is_boolean()) {
            return value.get<bool>() ? "true" : "false";
        }
    }
    return std::nullopt;
}

std::string urlDecode(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());

    auto hexToInt = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };

    for (std::size_t i = 0; i < value.size(); ++i) {
        char ch = value[i];
        if (ch == '+' ) {
            decoded.push_back(' ');
            continue;
        }
        if (ch == '%' && i + 2 < value.size()) {
            const int hi = hexToInt(value[i + 1]);
            const int lo = hexToInt(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(ch);
    }

    return decoded;
}

bool parseTokenQueryString(std::string_view input, LoginToken& out) {
    std::string query(input);
    const std::size_t qPos = query.find('?');
    if (qPos != std::string::npos) {
        query = query.substr(qPos + 1);
    }

    std::string tokenId;
    std::string token;
    std::size_t start = 0;
    while (start < query.size()) {
        const std::size_t amp = query.find('&', start);
        const std::size_t end = (amp == std::string::npos) ? query.size() : amp;
        const std::string part = query.substr(start, end - start);
        const std::size_t eq = part.find('=');
        if (eq != std::string::npos) {
            const std::string key = urlDecode(part.substr(0, eq));
            const std::string value = urlDecode(part.substr(eq + 1));
            if (key == "tokenId" || key == "token_id" || key == "tokenID") {
                tokenId = value;
            } else if (key == "token" || key == "accessToken" || key == "access_token") {
                token = value;
            }
        }
        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }

    if (!tokenId.empty() && !token.empty()) {
        out.tokenId = tokenId;
        out.token = token;
        return true;
    }
    return false;
}

bool tryExtractTokenFromText(const std::string& text, LoginToken& out) {
    if (parseTokenQueryString(text, out)) {
        return true;
    }

    std::smatch idMatch;
    std::smatch tokenMatch;
    const std::regex tokenIdRe(R"((tokenId|token_id|tokenID)\s*["']?\s*[:=]\s*["']([^"']+)["'])", std::regex::icase);
    const std::regex tokenRe(R"((token|accessToken|access_token)\s*["']?\s*[:=]\s*["']([^"']+)["'])", std::regex::icase);
    if (std::regex_search(text, idMatch, tokenIdRe) &&
        std::regex_search(text, tokenMatch, tokenRe) &&
        idMatch.size() > 2 && tokenMatch.size() > 2) {
        out.tokenId = idMatch[2].str();
        out.token = tokenMatch[2].str();
        return !out.tokenId.empty() && !out.token.empty();
    }

    std::string trimmed = trimCopy(text);
    if (trimmed.size() >= 2 &&
        ((trimmed.front() == '"' && trimmed.back() == '"') ||
         (trimmed.front() == '\'' && trimmed.back() == '\''))) {
        trimmed = trimmed.substr(1, trimmed.size() - 2);
    }
    return parseCombinedToken(trimmed, out);
}

bool tryExtractTokenObject(const nlohmann::json& node, LoginToken& out) {
    if (!node.is_object()) {
        return false;
    }

    const auto tokenId = getStringByKeys(node, {"tokenId", "token_id", "tokenID"});
    const auto token = getStringByKeys(node, {"token", "accessToken", "access_token"});
    if (tokenId && token) {
        out.tokenId = *tokenId;
        out.token = *token;
        return true;
    }

    const auto combined = getStringByKeys(node, {"loginToken", "combinedToken", "combined", "authToken"});
    if (combined && parseCombinedToken(*combined, out)) {
        return true;
    }
    return false;
}

bool extractLoginToken(const nlohmann::json& root, LoginToken& out) {
    if (root.is_string()) {
        return parseCombinedToken(root.get<std::string>(), out);
    }

    if (tryExtractTokenObject(root, out)) {
        return true;
    }

    if (root.is_object()) {
        static constexpr const char* kNestedKeys[] = {"data", "result", "payload", "response", "body"};
        for (const char* key : kNestedKeys) {
            if (!root.contains(key)) {
                continue;
            }
            const auto& nested = root.at(key);
            if (nested.is_string()) {
                if (parseCombinedToken(nested.get<std::string>(), out) ||
                    parseTokenQueryString(nested.get<std::string>(), out)) {
                    return true;
                }
            } else if (tryExtractTokenObject(nested, out)) {
                return true;
            } else if (nested.is_array()) {
                for (const auto& item : nested) {
                    if (extractLoginToken(item, out)) {
                        return true;
                    }
                }
            }
        }
    }

    if (root.is_array()) {
        for (const auto& item : root) {
            if (extractLoginToken(item, out)) {
                return true;
            }
        }
    }

    return false;
}

} // namespace

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

std::optional<MetaInfo> HttpClient::fetchMetaInfo(const std::string& region) {
    std::string url = std::format("https://{}.api.waruniverse.space/meta-info", region);
    
    auto response = get(url);
    if (!response) {
        return std::nullopt;
    }
    
    try {
        auto j = nlohmann::json::parse(*response);
        MetaInfo info;
        from_json(j, info);
        return info;
    } catch (const std::exception& e) {
        lastError_ = std::format("JSON parse error: {}", e.what());
        return std::nullopt;
    }
}

std::optional<LoginToken> HttpClient::getLoginToken(
    const std::string& baseUrl,
    const std::string& username,
    const std::string& password
) {
    // URL: {baseUrl}/auth-api/v3/login/{username}/token?password={password}
    std::string normalizedBaseUrl = baseUrl;
    while (!normalizedBaseUrl.empty() && normalizedBaseUrl.back() == '/') {
        normalizedBaseUrl.pop_back();
    }
    const std::string encodedUsername = urlEncode(username);
    const std::string encodedPassword = urlEncode(password);
    std::string url = std::format("{}/auth-api/v3/login/{}/token?password={}",
        normalizedBaseUrl, encodedUsername, encodedPassword);
    
    auto response = get(url);
    if (!response) {
        return std::nullopt;
    }

    std::string body = stripUtf8Bom(trimCopy(*response));
    if (body.empty()) {
        lastError_ = "Login response is empty";
        return std::nullopt;
    }

    try {
        auto j = nlohmann::json::parse(body);
        LoginToken token;
        if (extractLoginToken(j, token)) {
            return token;
        }

        if (tryExtractTokenFromText(body, token)) {
            return token;
        }

        lastError_ = std::format(
            "Login response JSON missing token fields. Body: {}",
            responseSnippet(body));
        return std::nullopt;
    } catch (const std::exception& e) {
        LoginToken token;
        if (tryExtractTokenFromText(body, token)) {
            return token;
        }

        lastError_ = std::format("JSON parse error: {}. Body: {}", e.what(), responseSnippet(body));
        return std::nullopt;
    }
}

std::optional<std::string> HttpClient::get(const std::string& url) {
    ParsedUrl parsed;
    if (!parseUrl(url, parsed, lastError_)) {
        return std::nullopt;
    }

    try {
        auto response = performGetRequest(parsed, true);
        if (response.body) {
            return response.body;
        }

        if (parsed.https &&
            response.error == httplib::Error::SSLServerVerification &&
            isTrustedWarUniverseHost(parsed.host)) {
            response = performGetRequest(parsed, false);
            if (response.body) {
                return response.body;
            }
        }

        lastError_ = response.detail.empty() ? "HTTP request failed" : response.detail;
        return std::nullopt;
    } catch (const std::exception& e) {
        lastError_ = std::format("HTTP exception: {}", e.what());
        return std::nullopt;
    }
}

std::optional<std::string> HttpClient::post(const std::string& url, const std::string& body, const std::string& contentType) {
    ParsedUrl parsed;
    if (!parseUrl(url, parsed, lastError_)) {
        return std::nullopt;
    }

    try {
        auto response = performPostRequest(parsed, body, contentType, true);
        if (response.body) {
            return response.body;
        }

        if (parsed.https &&
            response.error == httplib::Error::SSLServerVerification &&
            isTrustedWarUniverseHost(parsed.host)) {
            response = performPostRequest(parsed, body, contentType, false);
            if (response.body) {
                return response.body;
            }
        }

        lastError_ = response.detail.empty() ? "HTTP POST failed" : response.detail;
        return std::nullopt;
    } catch (const std::exception& e) {
        lastError_ = std::format("HTTP exception: {}", e.what());
        return std::nullopt;
    }
}

} // namespace dynamo
