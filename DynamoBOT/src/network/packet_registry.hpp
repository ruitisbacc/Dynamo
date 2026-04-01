#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <typeindex>
#include <vector>

namespace dynamo {

class KryoBuffer;
class ISerializable;

// Packet type IDs - must match Java Kryo registration order from PacketHook.java
// Starting from 0 for Object.class
enum class PacketId : int32_t {
    // Base types (0-6)
    Object = 0,
    ObjectArray = 1,
    Int = 2,
    IntArray = 3,
    String = 4,
    StringArray = 5,
    
    // Auth packets (6-13)
    AuthRequestPacket = 6,
    AuthAnswerPacket = 7,
    SignUpRequestPacket = 8,
    SignUpResponsePacket = 9,
    AuthFractionRequestPacket = 10,
    AuthFractionAnswerPacket = 11,
    MapConnectRequestPacket = 12,
    MapConnectAnswerPacket = 13,
    
    // Equipment classes (14-25)
    Ammo = 14,
    LaserAmmo = 15,
    AmmoArray = 16,
    Drone = 17,
    DroneArray = 18,
    Ares = 19,
    Nimbus = 20,
    Equipment = 21,
    EquipmentArray = 22,
    LaserGun = 23,
    SpeedGen = 24,
    ShieldGen = 25,
    
    // Ship packets (26-34)
    ShipInPacket = 26,
    ShipInPacketArray = 27,
    EquipRequestPacket = 28,
    EquipResponsePacket = 29,
    EquipMoveRequestPacket = 30,
    EquipMoveResponsePacket = 31,
    SellItemRequestPacket = 32,
    SellItemResponsePacket = 33,
    
    // Auction (34-37)
    AuctionItemsRequestPacket = 34,
    AuctionItemsResponsePacket = 35,
    AuctionBidRequestPacket = 36,
    AuctionBidResponsePacket = 37,
    
    // Clan (38-45)
    ClanActionRequestPacket = 38,
    ClanActionResponsePacket = 39,
    ClanMemberInPacket = 40,
    ClanMemberInPacketArray = 41,
    ClanInPacket = 42,
    ClanInPacketArray = 43,
    
    // Shop (44-47)
    ShopItemsRequestPacket = 44,
    ShopItemsResponsePacket = 45,
    ShopBuyRequestPacket = 46,
    ShopBuyResponsePacket = 47,
    
    // Stats (48-52)
    StatsRequest = 48,
    GeneralStatsResponse = 49,
    ScoreStatsResponse = 50,
    ClanStatsResponse = 51,
    OnlineStatsResponse = 52,
    
    // Game state (53-60)
    GameStateResponsePacket = 53,
    UserActionsPacket = 54,
    UserActionsPacketUserAction = 55,
    UserActionsPacketUserActionArray = 56,
    GameStateResponsePacketShipInResponse = 57,
    GameStateResponsePacketShipInResponseArray = 58,
    
    // Chat (59-62)
    ChatMessageRequest = 59,
    ChatMessageResponse = 60,
    EventResponsePacket = 61,
    
    // Map (62-67)
    MapInfoPacket = 62,
    MapInfoPacketTPort = 63,
    MapInfoPacketTPortArray = 64,
    MessageResponsePacket = 65,
    TeleportRequestPacket = 66,
    TeleportResponsePacket = 67,
    
    // Repair (68-69)
    RepairRequestPacket = 68,
    RepairResponsePacket = 69,
    
    // Extensions and ammo types (70-74)
    Extension = 70,
    ExtensionState = 71,
    EnergyAmmo = 72,
    RocketAmmo = 73,
    
    // Events (74-78)
    GameEvent = 74,
    GameEventArray = 75,
    SpaceballEventInfo = 76,
    FractionChangeRequest = 77,
    FractionChangeResponse = 78,
    
    // More packets (79-121)
    RepairCostRequestPacket = 79,
    RepairCostResponsePacket = 80,
    IntArrayArray = 81,
    CollectableCollectRequest = 82,
    RocketShotRequest = 83,
    RocketSwitchRequest = 84,
    AutoRocketRequest = 85,
    ClanDiplomacyInPacket = 86,
    ClanDiplomacyInPacketArray = 87,
    ChangeCredentialsRequest = 88,
    ChangeCredentialsResponse = 89,
    ChatAvailableRoomsRequestPacket = 90,
    MissionsActionRequestPacket = 91,
    MissionsActionResponsePacket = 92,
    DroneCover = 93,
    HangarInPacket = 94,
    HangarInPacketArray = 95,
    EquipHangarActionRequest = 96,
    EquipHangarActionResponse = 97,
    UserInfoResponsePacket = 98,
    ChangedParameter = 99,
    ChangedParameterArray = 100,
    CollectableInPacket = 101,
    CollectableInPacketArray = 102,
    ResourcesPack = 103,
    ResourcesActionRequestPacket = 104,
    ResourcesInfoResponsePacket = 105,
    ResourcesTradeInfoResponsePacket = 106,
    ResourceInfoArray = 107,
    ResourceInfo = 108,
    ResourcesInfoResponsePacketEnrichmentInfoArray = 109,
    ResourcesInfoResponsePacketEnrichmentInfo = 110,
    ConvoyEventInfo = 111,
    QuestsActionRequestPacket = 112,
    QuestsActionResponsePacket = 113,
    ObjectArrayArray = 114,
    RewardItem = 115,
    RewardItemArray = 116,
    Reward = 117,
    RegularStatsResponse = 118,
    MapEvent = 119,
    MapEventArray = 120,
    
    // ClientPackets.getPacketsToRegister() adds these (121-131)
    SquadsNetPacket = 121,
    ChatNetPacket = 122,
    ClientOnPausePacket = 123,
    ClientOnResumePacket = 124,
    AuctionNetPacket = 125,
    AuctionNotificationNetPacket = 126,
    ClientInfoNetPacket = 127,
    ApiRequestPacket = 128,
    ApiResponseNetStatus = 129,
    ApiResponsePacket = 130,
    ApiNotification = 131,
    
    Unknown = -1
};

// Packet factory and registry
class PacketRegistry {
public:
    using DeserializeFunc = std::function<std::unique_ptr<ISerializable>(KryoBuffer&)>;
    
    static PacketRegistry& instance();
    
    // Register a packet type
    template<typename T>
    void registerPacket(PacketId id) {
        deserializers_[static_cast<int32_t>(id)] = [](KryoBuffer& buf) {
            auto packet = std::make_unique<T>();
            packet->deserialize(buf);
            return packet;
        };
        typeToId_[std::type_index(typeid(T))] = id;
    }
    
    // Get packet ID from type
    template<typename T>
    PacketId getPacketId() const {
        auto it = typeToId_.find(std::type_index(typeid(T)));
        return it != typeToId_.end() ? it->second : PacketId::Unknown;
    }
    
    // Deserialize a packet
    std::unique_ptr<ISerializable> deserialize(KryoBuffer& buffer);
    
    // Get packet ID from buffer (peek, doesn't consume)
    PacketId peekPacketId(const std::vector<uint8_t>& data) const;
    
    // Get packet name for debugging
    static std::string getPacketName(PacketId id);
    
private:
    PacketRegistry();
    void registerAllPackets();
    
    std::unordered_map<int32_t, DeserializeFunc> deserializers_;
    std::unordered_map<std::type_index, PacketId> typeToId_;
};

} // namespace dynamo
