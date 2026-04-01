#pragma once

#include "network/codec/kryo_serializer.hpp"
#include <string>
#include <cstdint>

namespace dynamo {

// API packets for the new token-based authentication system
// Used with uri "auth/token-login" and other API endpoints

struct ApiRequestPacket : public ISerializable {
    int32_t requestId{0};
    std::string uri;
    std::string requestDataJson;
    
    // Kryo FieldSerializer alphabetical order: requestDataJson, requestId, uri
    // (D < I in "requestDataJson" vs "requestId")
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(requestDataJson);
        buf.writeKryoInt(requestId);
        buf.writeString(uri);
    }

    void deserialize(KryoBuffer& buf) override {
        requestDataJson = buf.readString();
        requestId = buf.readKryoInt();
        uri = buf.readString();
    }
};

struct ApiResponsePacket : public ISerializable {
    int32_t requestId{0};
    std::string uri;
    std::string responseInfoJson;
    std::string responseDataJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(requestId);
        buf.writeString(responseDataJson);
        buf.writeString(responseInfoJson);
        buf.writeString(uri);
    }
    
    // Kryo FieldSerializer alphabetical order: requestId, responseDataJson, responseInfoJson, uri
    void deserialize(KryoBuffer& buf) override {
        requestId = buf.readKryoInt();
        bool isNull;
        responseDataJson = buf.readStringOrNull(isNull);
        responseInfoJson = buf.readStringOrNull(isNull);
        uri = buf.readString();
    }
};

struct ApiResponseNetStatus : public ISerializable {
    int32_t requestId{0};
    int32_t status{0};  // HTTP-like status code
    std::string uri;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(requestId);
        buf.writeKryoInt(status);
        buf.writeString(uri);
    }
    
    void deserialize(KryoBuffer& buf) override {
        requestId = buf.readKryoInt();
        status = buf.readKryoInt();
        uri = buf.readString();
    }
};

// ApiNotification - server push notifications
// Java fields: key (String), notificationJsonString (String)
// Kryo FieldSerializer reads fields in alphabetical order by field name
// So: key first, then notificationJsonString
struct ApiNotification : public ISerializable {
    std::string key;                    // notification type (e.g., "map-info")
    std::string notificationJsonString; // JSON payload
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(key);
        buf.writeString(notificationJsonString);
    }
    
    void deserialize(KryoBuffer& buf) override {
        key = buf.readString();
        bool isNull;
        notificationJsonString = buf.readStringOrNull(isNull);
    }
};

// Known API URIs (from WuApi.java)
namespace WuApiUri {
    inline constexpr const char* AUTH_SIGN_IN = "auth/signin";
    inline constexpr const char* AUTH_TOKEN_LOGIN = "auth/token-login";
    inline constexpr const char* TOKEN_LOGIN = AUTH_TOKEN_LOGIN;  // Alias used by GameEngine
    inline constexpr const char* AUTH_LOGOUT = "auth/logout";
    inline constexpr const char* PROFILE_INFO = "profile/info";
    inline constexpr const char* PROFILE_PARAMETERS = "profile/parameters";
    inline constexpr const char* PROFILE_INVENTORY = "profile/inventory";
    inline constexpr const char* PROFILE_ACHIEVEMENTS = "profile/achievements";
    inline constexpr const char* MAP_CONNECT = "map/connect";
    inline constexpr const char* MAP_INFO = "map/info";  // Map info notification key
    inline constexpr const char* SHOP_ITEMS = "shop/items/v2";
    inline constexpr const char* SHOP_BUY = "shop/buy";
    inline constexpr const char* MISSIONS_LIST = "missions/list";
    inline constexpr const char* MISSIONS_ACTION = "missions/action";
    inline constexpr const char* QUESTS_LIST = "quests/list";
    inline constexpr const char* QUESTS_ACTION = "quests/action";
    inline constexpr const char* CLAN_INFO = "clan/info";
    inline constexpr const char* CLAN_MEMBERS = "clan/members";
    inline constexpr const char* RANKING = "ranking";
}

} // namespace dynamo
