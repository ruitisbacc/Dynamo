#pragma once

#include "network/codec/kryo_serializer.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace dynamo {

// MapInfoPacket - map information with teleporters
struct MapInfoPacketTPort : public ISerializable {
    int32_t subtype{0};
    int32_t type{0};
    int32_t x{0};
    int32_t y{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(subtype);
        buf.writeKryoInt(type);
        buf.writeKryoInt(x);
        buf.writeKryoInt(y);
    }
    
    void deserialize(KryoBuffer& buf) override {
        subtype = buf.readKryoInt();
        type = buf.readKryoInt();
        x = buf.readKryoInt();
        y = buf.readKryoInt();
    }
};

struct MapInfoPacket : public ISerializable {
    int32_t height{0};
    int32_t mapId{0};
    std::string name;
    bool spaceStation{false};
    float ssx{0.0f};
    float ssy{0.0f};
    std::vector<MapInfoPacketTPort> teleports;
    std::vector<int32_t> tradeStation;
    int32_t width{0};
    
    void serialize(KryoBuffer& buf) const override {
        (void)buf;
        // Typically not sent by client
    }
    
    void deserialize(KryoBuffer& buf) override {
        // 1. height (int)
        height = buf.readKryoInt();
        
        // 2. mapId (int)
        mapId = buf.readKryoInt();
        
        // 3. name (String)
        name = buf.readString();
        
        // 4. spaceStation (boolean)
        spaceStation = buf.readBool();
        
        // 5. ssx (float)
        ssx = buf.readFloat();
        
        // 6. ssy (float)
        ssy = buf.readFloat();
        
        // 7. teleports (MapInfoPacket$TPort[])
        int32_t tpArrayClass = buf.readVarInt(true);
        if (tpArrayClass != 0) {
            int32_t size = buf.readVarInt(true);
            if (size > 0) size--;
            teleports.resize(size);
            for (auto& tp : teleports) {
                int32_t eClass = buf.readVarInt(true);
                if (eClass != 0) tp.deserialize(buf);
            }
        }
        
        // 8. tradeStation (int[])
        int32_t size = buf.readVarInt(true);
        if (size != 0) {
            if (size > 0) size--;
            tradeStation.resize(size);
            for (auto& ts : tradeStation) {
                ts = buf.readKryoInt();
            }
        }
        
        // 9. width (int)
        width = buf.readKryoInt();
    }
};

// TeleportRequestPacket - request to use teleporter
struct TeleportRequestPacket : public ISerializable {
    int32_t teleporterId{0};
    
    TeleportRequestPacket() = default;
    explicit TeleportRequestPacket(int32_t id) : teleporterId(id) {}
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(teleporterId);
    }
    
    void deserialize(KryoBuffer& buf) override {
        teleporterId = buf.readKryoInt();
    }
};

// TeleportResponsePacket - teleport result
struct TeleportResponsePacket : public ISerializable {
    bool success{false};
    int32_t targetMapId{0};
    int32_t targetX{0};
    int32_t targetY{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(success);
        buf.writeKryoInt(targetMapId);
        buf.writeKryoInt(targetX);
        buf.writeKryoInt(targetY);
    }
    
    void deserialize(KryoBuffer& buf) override {
        success = buf.readBool();
        targetMapId = buf.readKryoInt();
        targetX = buf.readKryoInt();
        targetY = buf.readKryoInt();
    }
};

// RepairRequestPacket - request repair
struct RepairRequestPacket : public ISerializable {
    RepairRequestPacket() = default;
    
    void serialize(KryoBuffer& buf) const override {
        (void)buf;
        // No fields
    }
    
    void deserialize(KryoBuffer& buf) override {
        (void)buf;
        // No fields
    }
};

// RepairResponsePacket - repair result
struct RepairResponsePacket : public ISerializable {
    int32_t status{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(status);
    }
    
    void deserialize(KryoBuffer& buf) override {
        status = buf.readKryoInt();
    }
};

// RepairCostRequestPacket
struct RepairCostRequestPacket : public ISerializable {
    void serialize(KryoBuffer& buf) const override {
        (void)buf;
        // No fields
    }
    
    void deserialize(KryoBuffer& buf) override {
        (void)buf;
        // No fields
    }
};

// RepairCostResponsePacket
struct RepairCostResponsePacket : public ISerializable {
    int32_t cost{0};
    int32_t currentHealth{0};
    int32_t maxHealth{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(cost);
        buf.writeKryoInt(currentHealth);
        buf.writeKryoInt(maxHealth);
    }
    
    void deserialize(KryoBuffer& buf) override {
        cost = buf.readKryoInt();
        currentHealth = buf.readKryoInt();
        maxHealth = buf.readKryoInt();
    }
};

// ChatMessageRequest - send chat message
struct ChatMessageRequest : public ISerializable {
    std::string message;
    std::string room;
    
    ChatMessageRequest() = default;
    ChatMessageRequest(const std::string& r, const std::string& m) 
        : room(r), message(m) {}
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(message);
        buf.writeString(room);
    }
    
    void deserialize(KryoBuffer& buf) override {
        message = buf.readString();
        room = buf.readString();
    }
};

// ChatMessageResponse - received chat message
struct ChatMessageResponse : public ISerializable {
    std::string message;
    std::string room;
    std::string sender;
    int64_t timestamp{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(message);
        buf.writeString(room);
        buf.writeString(sender);
        buf.writeKryoLong(timestamp);
    }
    
    void deserialize(KryoBuffer& buf) override {
        message = buf.readString();
        room = buf.readString();
        sender = buf.readString();
        timestamp = buf.readKryoLong();
    }
};

// ChatNetPacket - newer chat system using JSON
struct ChatNetPacket : public ISerializable {
    std::string json;
    bool synchronize{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(json);
        buf.writeBool(synchronize);
    }
    
    void deserialize(KryoBuffer& buf) override {
        json = buf.readString();
        synchronize = buf.readBool();
    }
};

// SquadsNetPacket - squad/group system
struct SquadsNetPacket : public ISerializable {
    std::string json;
    bool synchronize{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(json);
        buf.writeBool(synchronize);
    }
    
    void deserialize(KryoBuffer& buf) override {
        json = buf.readString();
        synchronize = buf.readBool();
    }
};

// ClientOnPausePacket - notify server client is paused (tab unfocused)
struct ClientOnPausePacket : public ISerializable {
    void serialize(KryoBuffer& buf) const override { (void)buf; }
    void deserialize(KryoBuffer& buf) override { (void)buf; }
};

// ClientOnResumePacket - notify server client resumed
struct ClientOnResumePacket : public ISerializable {
    void serialize(KryoBuffer& buf) const override { (void)buf; }
    void deserialize(KryoBuffer& buf) override { (void)buf; }
};

// ClientInfoNetPacket - client info sent periodically
struct ClientInfoNetPacket : public ISerializable {
    std::string json;
    bool synchronize{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(json);
        buf.writeBool(synchronize);
    }
    
    void deserialize(KryoBuffer& buf) override {
        json = buf.readString();
        synchronize = buf.readBool();
    }
};

} // namespace dynamo
