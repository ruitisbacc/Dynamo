#include "packet_registry.hpp"
#include "kryo_serializer.hpp"
#include "packets/auth_packets.hpp"
#include "packets/api_packets.hpp"
#include "packets/game_packets.hpp"
#include "packets/action_packets.hpp"
#include "packets/map_packets.hpp"
#include "packets/extra_packets.hpp"

namespace dynamo {

PacketRegistry& PacketRegistry::instance() {
    static PacketRegistry registry;
    return registry;
}

PacketRegistry::PacketRegistry() {
    registerAllPackets();
}

void PacketRegistry::registerAllPackets() {
    // Register auth packets
    registerPacket<AuthRequestPacket>(PacketId::AuthRequestPacket);
    registerPacket<AuthAnswerPacket>(PacketId::AuthAnswerPacket);
    registerPacket<MapConnectRequestPacket>(PacketId::MapConnectRequestPacket);
    registerPacket<MapConnectAnswerPacket>(PacketId::MapConnectAnswerPacket);
    
    // Register game state packets
    registerPacket<GameStateResponsePacket>(PacketId::GameStateResponsePacket);
    registerPacket<UserInfoResponsePacket>(PacketId::UserInfoResponsePacket);
    registerPacket<MessageResponsePacket>(PacketId::MessageResponsePacket);
    registerPacket<EventResponsePacket>(PacketId::EventResponsePacket);
    
    // Register action packets
    registerPacket<UserActionsPacket>(PacketId::UserActionsPacket);
    registerPacket<CollectableCollectRequest>(PacketId::CollectableCollectRequest);
    registerPacket<RocketShotRequest>(PacketId::RocketShotRequest);
    registerPacket<RocketSwitchRequest>(PacketId::RocketSwitchRequest);
    registerPacket<AutoRocketRequest>(PacketId::AutoRocketRequest);
    
    // Register map packets
    registerPacket<MapInfoPacket>(PacketId::MapInfoPacket);
    registerPacket<TeleportRequestPacket>(PacketId::TeleportRequestPacket);
    registerPacket<TeleportResponsePacket>(PacketId::TeleportResponsePacket);
    registerPacket<RepairRequestPacket>(PacketId::RepairRequestPacket);
    registerPacket<RepairResponsePacket>(PacketId::RepairResponsePacket);
    registerPacket<RepairCostRequestPacket>(PacketId::RepairCostRequestPacket);
    registerPacket<RepairCostResponsePacket>(PacketId::RepairCostResponsePacket);
    
    // Register chat packets
    registerPacket<ChatMessageRequest>(PacketId::ChatMessageRequest);
    registerPacket<ChatMessageResponse>(PacketId::ChatMessageResponse);
    registerPacket<ChatNetPacket>(PacketId::ChatNetPacket);
    registerPacket<SquadsNetPacket>(PacketId::SquadsNetPacket);
    
    // Register client info packets
    registerPacket<ClientOnPausePacket>(PacketId::ClientOnPausePacket);
    registerPacket<ClientOnResumePacket>(PacketId::ClientOnResumePacket);
    registerPacket<ClientInfoNetPacket>(PacketId::ClientInfoNetPacket);
    
    // Register API packets
    registerPacket<ApiRequestPacket>(PacketId::ApiRequestPacket);
    registerPacket<ApiResponsePacket>(PacketId::ApiResponsePacket);
    registerPacket<ApiNotification>(PacketId::ApiNotification);
    
    // Register shop packets
    registerPacket<ShopItemsRequestPacket>(PacketId::ShopItemsRequestPacket);
    registerPacket<ShopItemsResponsePacket>(PacketId::ShopItemsResponsePacket);
    registerPacket<ShopBuyRequestPacket>(PacketId::ShopBuyRequestPacket);
    registerPacket<ShopBuyResponsePacket>(PacketId::ShopBuyResponsePacket);
    
    // Register clan packets
    registerPacket<ClanActionRequestPacket>(PacketId::ClanActionRequestPacket);
    registerPacket<ClanActionResponsePacket>(PacketId::ClanActionResponsePacket);
    registerPacket<ClanInPacket>(PacketId::ClanInPacket);
    registerPacket<ClanMemberInPacket>(PacketId::ClanMemberInPacket);
    registerPacket<ClanDiplomacyInPacket>(PacketId::ClanDiplomacyInPacket);
    
    // Register stats packets
    registerPacket<StatsRequest>(PacketId::StatsRequest);
    registerPacket<GeneralStatsResponse>(PacketId::GeneralStatsResponse);
    registerPacket<ScoreStatsResponse>(PacketId::ScoreStatsResponse);
    registerPacket<ClanStatsResponse>(PacketId::ClanStatsResponse);
    registerPacket<OnlineStatsResponse>(PacketId::OnlineStatsResponse);
    registerPacket<RegularStatsResponse>(PacketId::RegularStatsResponse);
    
    // Register equip packets
    registerPacket<EquipRequestPacket>(PacketId::EquipRequestPacket);
    registerPacket<EquipResponsePacket>(PacketId::EquipResponsePacket);
    registerPacket<EquipMoveRequestPacket>(PacketId::EquipMoveRequestPacket);
    registerPacket<EquipMoveResponsePacket>(PacketId::EquipMoveResponsePacket);
    registerPacket<SellItemRequestPacket>(PacketId::SellItemRequestPacket);
    registerPacket<SellItemResponsePacket>(PacketId::SellItemResponsePacket);
    registerPacket<EquipHangarActionRequest>(PacketId::EquipHangarActionRequest);
    registerPacket<EquipHangarActionResponse>(PacketId::EquipHangarActionResponse);
    registerPacket<HangarInPacket>(PacketId::HangarInPacket);
    
    // Register auction packets
    registerPacket<AuctionItemsRequestPacket>(PacketId::AuctionItemsRequestPacket);
    registerPacket<AuctionItemsResponsePacket>(PacketId::AuctionItemsResponsePacket);
    registerPacket<AuctionBidRequestPacket>(PacketId::AuctionBidRequestPacket);
    registerPacket<AuctionBidResponsePacket>(PacketId::AuctionBidResponsePacket);
    registerPacket<AuctionNetPacket>(PacketId::AuctionNetPacket);
    registerPacket<AuctionNotificationNetPacket>(PacketId::AuctionNotificationNetPacket);
    
    // Register mission/quest packets
    registerPacket<MissionsActionRequestPacket>(PacketId::MissionsActionRequestPacket);
    registerPacket<MissionsActionResponsePacket>(PacketId::MissionsActionResponsePacket);
    registerPacket<QuestsActionRequestPacket>(PacketId::QuestsActionRequestPacket);
    registerPacket<QuestsActionResponsePacket>(PacketId::QuestsActionResponsePacket);
    
    // Register resource packets
    registerPacket<ResourcesActionRequestPacket>(PacketId::ResourcesActionRequestPacket);
    registerPacket<ResourcesInfoResponsePacket>(PacketId::ResourcesInfoResponsePacket);
    registerPacket<ResourcesTradeInfoResponsePacket>(PacketId::ResourcesTradeInfoResponsePacket);
    
    // Register misc packets
    registerPacket<FractionChangeRequest>(PacketId::FractionChangeRequest);
    registerPacket<FractionChangeResponse>(PacketId::FractionChangeResponse);
    registerPacket<ChangeCredentialsRequest>(PacketId::ChangeCredentialsRequest);
    registerPacket<ChangeCredentialsResponse>(PacketId::ChangeCredentialsResponse);
}

std::unique_ptr<ISerializable> PacketRegistry::deserialize(KryoBuffer& buffer) {
    // Read class ID (varint)
    int32_t classId = buffer.readVarInt(true) - 14;
    
    auto it = deserializers_.find(classId);
    if (it == deserializers_.end()) {
        throw std::runtime_error("Unknown packet ID: " + std::to_string(classId));
    }
    
    return it->second(buffer);
}

PacketId PacketRegistry::peekPacketId(const std::vector<uint8_t>& data) const {
    if (data.empty()) return PacketId::Unknown;
    
    KryoBuffer buf(data);
    int32_t classId = buf.readVarInt(true) - 14;
    return static_cast<PacketId>(classId);
}

std::string PacketRegistry::getPacketName(PacketId id) {
    static const std::unordered_map<PacketId, std::string> names = {
        {PacketId::Object, "Object"},
        {PacketId::AuthRequestPacket, "AuthRequestPacket"},
        {PacketId::AuthAnswerPacket, "AuthAnswerPacket"},
        {PacketId::SignUpResponsePacket, "SignUpResponsePacket"},
        {PacketId::AuthFractionAnswerPacket, "AuthFractionAnswerPacket"},
        {PacketId::MapConnectRequestPacket, "MapConnectRequestPacket"},
        {PacketId::MapConnectAnswerPacket, "MapConnectAnswerPacket"},
        {PacketId::ChangeCredentialsRequest, "ChangeCredentialsRequest"},
        {PacketId::ChangeCredentialsResponse, "ChangeCredentialsResponse"},
        {PacketId::ChatAvailableRoomsRequestPacket, "ChatAvailableRoomsRequestPacket"},
        {PacketId::GameStateResponsePacket, "GameStateResponsePacket"},
        {PacketId::UserActionsPacket, "UserActionsPacket"},
        {PacketId::UserInfoResponsePacket, "UserInfoResponsePacket"},
        {PacketId::MessageResponsePacket, "MessageResponsePacket"},
        {PacketId::ApiRequestPacket, "ApiRequestPacket"},
        {PacketId::ApiResponsePacket, "ApiResponsePacket"},
        {PacketId::ApiNotification, "ApiNotification"},
        {PacketId::ChatNetPacket, "ChatNetPacket"},
        {PacketId::GameEvent, "GameEvent"},
        {PacketId::MapEvent, "MapEvent"},
    };
    
    auto it = names.find(id);
    return it != names.end() ? it->second : "Unknown(" + std::to_string(static_cast<int>(id)) + ")";
}

} // namespace dynamo
