#pragma once

#include "network/kryo_serializer.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace dynamo {

// ============================================================================
// SHOP PACKETS
// ============================================================================

struct ShopItemsRequestPacket : public ISerializable {
    int32_t categoryId{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(categoryId, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        categoryId = buf.readVarInt(true);
    }
};

struct ShopItemsResponsePacket : public ISerializable {
    std::string itemsJson;  // JSON array of shop items
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(itemsJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        itemsJson = buf.readString();
    }
};

struct ShopBuyRequestPacket : public ISerializable {
    std::string itemJson;  // Item to buy (serialized as Object)
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(itemJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        itemJson = buf.readString();
    }
};

struct ShopBuyResponsePacket : public ISerializable {
    int32_t status{0};  // 0 = success, other = error code
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(status, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        status = buf.readVarInt(true);
    }
};

// ============================================================================
// CLAN PACKETS
// ============================================================================

struct ClanActionRequestPacket : public ISerializable {
    int32_t actionId{0};
    std::string dataJson;
    
    // Action IDs (estimated from game behavior)
    static constexpr int32_t ACTION_CREATE = 1;
    static constexpr int32_t ACTION_INVITE = 2;
    static constexpr int32_t ACTION_KICK = 3;
    static constexpr int32_t ACTION_LEAVE = 4;
    static constexpr int32_t ACTION_PROMOTE = 5;
    static constexpr int32_t ACTION_DEMOTE = 6;
    static constexpr int32_t ACTION_DISBAND = 7;
    static constexpr int32_t ACTION_SET_DIPLOMACY = 8;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(actionId, true);
        buf.writeString(dataJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        actionId = buf.readVarInt(true);
        dataJson = buf.readString();
    }
};

struct ClanActionResponsePacket : public ISerializable {
    std::string errorMsg;
    bool success{false};
    
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

struct ClanInPacket : public ISerializable {
    int32_t factionId{0};
    int32_t id{0};
    int32_t memberCount{0};
    std::string name;
    std::string tag;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(factionId, true);
        buf.writeVarInt(id, true);
        buf.writeVarInt(memberCount, true);
        buf.writeString(name);
        buf.writeString(tag);
    }
    
    void deserialize(KryoBuffer& buf) override {
        factionId = buf.readVarInt(true);
        id = buf.readVarInt(true);
        memberCount = buf.readVarInt(true);
        name = buf.readString();
        tag = buf.readString();
    }
};

struct ClanMemberInPacket : public ISerializable {
    int32_t id{0};
    std::string name;
    bool online{false};
    int32_t rank{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(id, true);
        buf.writeString(name);
        buf.writeBool(online);
        buf.writeVarInt(rank, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        id = buf.readVarInt(true);
        name = buf.readString();
        online = buf.readBool();
        rank = buf.readVarInt(true);
    }
};

struct ClanDiplomacyInPacket : public ISerializable {
    int32_t clanId{0};
    int32_t relation{0};  // 0=neutral, 1=ally, 2=war
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(clanId, true);
        buf.writeVarInt(relation, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        clanId = buf.readVarInt(true);
        relation = buf.readVarInt(true);
    }
};

// ============================================================================
// STATS PACKETS
// ============================================================================

struct StatsRequest : public ISerializable {
    int32_t type{0};  // Which stats to request
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(type, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        type = buf.readVarInt(true);
    }
};

struct GeneralStatsResponse : public ISerializable {
    std::string statsJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(statsJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        statsJson = buf.readString();
    }
};

struct ScoreStatsResponse : public ISerializable {
    std::string statsJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(statsJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        statsJson = buf.readString();
    }
};

struct ClanStatsResponse : public ISerializable {
    std::string statsJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(statsJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        statsJson = buf.readString();
    }
};

struct OnlineStatsResponse : public ISerializable {
    int32_t totalOnline{0};
    std::string perMapJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(totalOnline, true);
        buf.writeString(perMapJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        totalOnline = buf.readVarInt(true);
        perMapJson = buf.readString();
    }
};

struct RegularStatsResponse : public ISerializable {
    std::string statsJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(statsJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        statsJson = buf.readString();
    }
};

// ============================================================================
// EQUIP PACKETS
// ============================================================================

struct EquipRequestPacket : public ISerializable {
    int32_t confi{0};  // Configuration index
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(confi, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        confi = buf.readVarInt(true);
    }
};

struct EquipResponsePacket : public ISerializable {
    bool success{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        success = buf.readBool();
    }
};

struct EquipMoveRequestPacket : public ISerializable {
    int32_t itemId{0};
    int32_t targetSlot{0};
    int32_t targetConfi{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(itemId, true);
        buf.writeVarInt(targetSlot, true);
        buf.writeVarInt(targetConfi, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        itemId = buf.readVarInt(true);
        targetSlot = buf.readVarInt(true);
        targetConfi = buf.readVarInt(true);
    }
};

struct EquipMoveResponsePacket : public ISerializable {
    bool success{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        success = buf.readBool();
    }
};

struct SellItemRequestPacket : public ISerializable {
    int32_t equipId{0};
    int32_t droneId{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(equipId, true);
        buf.writeVarInt(droneId, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        equipId = buf.readVarInt(true);
        droneId = buf.readVarInt(true);
    }
};

struct SellItemResponsePacket : public ISerializable {
    int32_t credits{0};
    bool success{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(credits, true);
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        credits = buf.readVarInt(true);
        success = buf.readBool();
    }
};

struct EquipHangarActionRequest : public ISerializable {
    int32_t actionId{0};
    int32_t hangarId{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(actionId, true);
        buf.writeVarInt(hangarId, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        actionId = buf.readVarInt(true);
        hangarId = buf.readVarInt(true);
    }
};

struct EquipHangarActionResponse : public ISerializable {
    bool success{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        success = buf.readBool();
    }
};

// ============================================================================
// HANGAR PACKETS
// ============================================================================

struct HangarInPacket : public ISerializable {
    bool activated{false};
    int32_t id{0};
    int32_t shipId{0};
    std::string shipName;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(activated);
        buf.writeVarInt(id, true);
        buf.writeVarInt(shipId, true);
        buf.writeString(shipName);
    }
    
    void deserialize(KryoBuffer& buf) override {
        activated = buf.readBool();
        id = buf.readVarInt(true);
        shipId = buf.readVarInt(true);
        shipName = buf.readString();
    }
};

// ============================================================================
// AUCTION PACKETS
// ============================================================================

struct AuctionItemsRequestPacket : public ISerializable {
    int32_t page{0};
    int32_t categoryId{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(page, true);
        buf.writeVarInt(categoryId, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        page = buf.readVarInt(true);
        categoryId = buf.readVarInt(true);
    }
};

struct AuctionItemsResponsePacket : public ISerializable {
    std::string itemsJson;
    int32_t totalPages{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(itemsJson);
        buf.writeVarInt(totalPages, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        itemsJson = buf.readString();
        totalPages = buf.readVarInt(true);
    }
};

struct AuctionBidRequestPacket : public ISerializable {
    int32_t auctionId{0};
    int32_t bidAmount{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(auctionId, true);
        buf.writeVarInt(bidAmount, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        auctionId = buf.readVarInt(true);
        bidAmount = buf.readVarInt(true);
    }
};

struct AuctionBidResponsePacket : public ISerializable {
    std::string errorMsg;
    bool success{false};
    
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

struct AuctionNetPacket : public ISerializable {
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

struct AuctionNotificationNetPacket : public ISerializable {
    std::string json;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(json);
    }
    
    void deserialize(KryoBuffer& buf) override {
        json = buf.readString();
    }
};

// ============================================================================
// MISSIONS & QUESTS PACKETS
// ============================================================================

struct MissionsActionRequestPacket : public ISerializable {
    int32_t actionId{0};
    std::string dataJson;
    
    static constexpr int32_t ACTION_START = 1;
    static constexpr int32_t ACTION_COMPLETE = 2;
    static constexpr int32_t ACTION_ABANDON = 3;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(actionId, true);
        buf.writeString(dataJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        actionId = buf.readVarInt(true);
        dataJson = buf.readString();
    }
};

struct MissionsActionResponsePacket : public ISerializable {
    std::string resultJson;
    bool success{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(resultJson);
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        resultJson = buf.readString();
        success = buf.readBool();
    }
};

struct QuestsActionRequestPacket : public ISerializable {
    int32_t actionId{0};
    std::string dataJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(actionId, true);
        buf.writeString(dataJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        actionId = buf.readVarInt(true);
        dataJson = buf.readString();
    }
};

struct QuestsActionResponsePacket : public ISerializable {
    std::string resultJson;
    bool success{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(resultJson);
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        resultJson = buf.readString();
        success = buf.readBool();
    }
};

// ============================================================================
// RESOURCES PACKETS
// ============================================================================

struct ResourcesActionRequestPacket : public ISerializable {
    int32_t actionId{0};
    std::string dataJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(actionId, true);
        buf.writeString(dataJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        actionId = buf.readVarInt(true);
        dataJson = buf.readString();
    }
};

struct ResourcesInfoResponsePacket : public ISerializable {
    std::string resourcesJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(resourcesJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        resourcesJson = buf.readString();
    }
};

struct ResourcesTradeInfoResponsePacket : public ISerializable {
    std::string tradeInfoJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(tradeInfoJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        tradeInfoJson = buf.readString();
    }
};

// ============================================================================
// MISC PACKETS
// ============================================================================

struct FractionChangeRequest : public ISerializable {
    int32_t fraction{0};  // Target faction ID
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(fraction, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        fraction = buf.readVarInt(true);
    }
};

struct FractionChangeResponse : public ISerializable {
    bool success{false};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(success);
    }
    
    void deserialize(KryoBuffer& buf) override {
        success = buf.readBool();
    }
};

struct ChangeCredentialsRequest : public ISerializable {
    std::string currentPassword;
    std::string newEmail;
    std::string newPassword;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(currentPassword);
        buf.writeString(newEmail);
        buf.writeString(newPassword);
    }
    
    void deserialize(KryoBuffer& buf) override {
        currentPassword = buf.readString();
        newEmail = buf.readString();
        newPassword = buf.readString();
    }
};

struct ChangeCredentialsResponse : public ISerializable {
    std::string errorMsg;
    bool success{false};
    
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

struct JsonNetPacket : public ISerializable {
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

struct PlayerLanguageRequestPacket : public ISerializable {
    std::string language;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(language);
    }
    
    void deserialize(KryoBuffer& buf) override {
        language = buf.readString();
    }
};

// ============================================================================
// EQUIPMENT DATA CLASSES
// ============================================================================

struct Equipment : public ISerializable {
    int32_t id{0};
    int32_t type{0};
    int32_t level{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(id, true);
        buf.writeVarInt(type, true);
        buf.writeVarInt(level, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        id = buf.readVarInt(true);
        type = buf.readVarInt(true);
        level = buf.readVarInt(true);
    }
};

struct LaserGun : public Equipment {
    // Inherits from Equipment
};

struct ShieldGen : public Equipment {
    // Inherits from Equipment
};

struct SpeedGen : public Equipment {
    // Inherits from Equipment
};

struct Drone : public ISerializable {
    int32_t id{0};
    int32_t type{0};
    int32_t level{0};
    std::vector<Equipment> equipment;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(id, true);
        buf.writeVarInt(type, true);
        buf.writeVarInt(level, true);
        buf.writeVarInt(static_cast<int32_t>(equipment.size()), true);
        for (const auto& eq : equipment) {
            eq.serialize(buf);
        }
    }
    
    void deserialize(KryoBuffer& buf) override {
        id = buf.readVarInt(true);
        type = buf.readVarInt(true);
        level = buf.readVarInt(true);
        int32_t size = buf.readVarInt(true);
        equipment.resize(size);
        for (auto& eq : equipment) {
            eq.deserialize(buf);
        }
    }
};

struct Ammo : public ISerializable {
    int32_t type{0};
    int32_t count{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(type, true);
        buf.writeVarInt(count, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        type = buf.readVarInt(true);
        count = buf.readVarInt(true);
    }
};

struct LaserAmmo : public Ammo {};
struct RocketAmmo : public Ammo {};
struct EnergyAmmo : public Ammo {};

// ============================================================================
// EVENT DATA CLASSES
// ============================================================================

struct SpaceballEventInfo : public ISerializable {
    std::string eventJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(eventJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        eventJson = buf.readString();
    }
};

struct ConvoyEventInfo : public ISerializable {
    std::string eventJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(eventJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        eventJson = buf.readString();
    }
};

struct ResourcesPack : public ISerializable {
    std::string resourcesJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(resourcesJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        resourcesJson = buf.readString();
    }
};

struct Reward : public ISerializable {
    std::string rewardJson;
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeString(rewardJson);
    }
    
    void deserialize(KryoBuffer& buf) override {
        rewardJson = buf.readString();
    }
};

struct RewardItem : public ISerializable {
    int32_t type{0};
    int32_t amount{0};
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeVarInt(type, true);
        buf.writeVarInt(amount, true);
    }
    
    void deserialize(KryoBuffer& buf) override {
        type = buf.readVarInt(true);
        amount = buf.readVarInt(true);
    }
};

} // namespace dynamo
