#pragma once

#include "network/codec/kryo_serializer.hpp"
#include <string>
#include <vector>

namespace dynamo {

// Auth packets matching Java classes from com.spaiowenta.commons.packets.auth

struct AuthRequestPacket : public ISerializable {
    std::vector<int> clientV{1, 233, 0};
    std::string lang{"en"};
    std::string login;
    std::string password;
    int platformId{0};  // 0 = web
    
    void serialize(KryoBuffer& buf) const override {
        // 1. clientV (int[])
        buf.writeVarInt(static_cast<int>(clientV.size()), true);
        for (int v : clientV) {
            buf.writeKryoInt(v);
        }
        
        // 2. lang (String)
        buf.writeString(lang);
        
        // 3. login (String)
        buf.writeString(login);
        
        // 4. password (String)
        buf.writeString(password);
        
        // 5. platformId (int)
        buf.writeKryoInt(platformId);
    }
    
    void deserialize(KryoBuffer& buf) override {
        // 1. clientV
        int size = buf.readVarInt(true);
        clientV.resize(size);
        for (int i = 0; i < size; ++i) {
            clientV[i] = buf.readKryoInt();
        }
        
        // 2. lang
        lang = buf.readString();
        
        // 3. login
        login = buf.readString();
        
        // 4. password
        password = buf.readString();
        
        // 5. platformId
        platformId = buf.readKryoInt();
    }
};

struct AuthAnswerPacket : public ISerializable {
    bool success{false};
    std::string ssid;
    std::string errorMsg;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(errorMsg);
        buf.writeString(ssid);
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        bool isNull;
        errorMsg = buf.readStringOrNull(isNull);
        ssid = buf.readStringOrNull(isNull);
        success = buf.readBool();
    }
};

struct SignUpRequestPacket : public ISerializable {
    std::string login;
    std::string password;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(login);
        buf.writeString(password);
    }
    
    void deserialize(KryoBuffer& buf) override {
        login = buf.readString();
        password = buf.readString();
    }
};

struct SignUpResponsePacket : public ISerializable {
    bool success{false};
    std::string errorMsg;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(errorMsg);
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        bool isNull;
        errorMsg = buf.readStringOrNull(isNull);
        success = buf.readBool();
    }
};

struct AuthFractionRequestPacket : public ISerializable {
    void serialize(KryoBuffer& buf) const override { (void)buf; }
    void deserialize(KryoBuffer& buf) override { (void)buf; }
};

struct AuthFractionAnswerPacket : public ISerializable {
    int32_t fraction{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(fraction);
    }
    
    void deserialize(KryoBuffer& buf) override {
        fraction = buf.readKryoInt();
    }
};

struct MapConnectRequestPacket : public ISerializable {
    std::string ssid;
    std::string lang{"en"};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(lang);
        buf.writeString(ssid);
    }
    
    void deserialize(KryoBuffer& buf) override {
        lang = buf.readString();
        ssid = buf.readString();
    }
};

struct MapConnectAnswerPacket : public ISerializable {
    bool success{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        success = buf.readBool();
    }
};

} // namespace dynamo
