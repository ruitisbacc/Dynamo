#pragma once

// All packet headers for DynamoBot
// Include this single file to get access to all packet types

#include "auth_packets.hpp"
#include "api_packets.hpp"
#include "game_packets.hpp"
#include "action_packets.hpp"
#include "map_packets.hpp"
#include "extra_packets.hpp"

namespace dynamo {

// Packet Categories:
//
// AUTH PACKETS (auth_packets.hpp):
//   - AuthRequestPacket, AuthAnswerPacket
//   - SignUpRequestPacket, SignUpResponsePacket
//   - AuthFractionRequestPacket, AuthFractionAnswerPacket
//   - MapConnectRequestPacket, MapConnectAnswerPacket
//
// API PACKETS (api_packets.hpp):
//   - ApiRequestPacket, ApiResponsePacket, ApiNotification
//   - ApiResponseNetStatus
//   - WuApiUri constants for all API endpoints
//
// GAME STATE PACKETS (game_packets.hpp):
//   - GameStateResponsePacket (main game tick)
//   - ShipInResponse, ChangedParameter
//   - GameEvent, MapEvent
//   - CollectableInPacket, ShipInPacket
//   - UserInfoResponsePacket
//   - MessageResponsePacket, EventResponsePacket
//
// ACTION PACKETS (action_packets.hpp):
//   - UserActionsPacket, UserAction
//   - CollectableCollectRequest
//   - RocketShotRequest, RocketSwitchRequest, AutoRocketRequest
//   - ActionType constants (MOVE, LOCK, ATTACK, etc.)
//
// MAP PACKETS (map_packets.hpp):
//   - MapInfoPacket, MapInfoPacketTPort
//   - TeleportRequestPacket, TeleportResponsePacket
//   - RepairRequestPacket, RepairResponsePacket
//   - RepairCostRequestPacket, RepairCostResponsePacket
//   - ChatMessageRequest, ChatMessageResponse
//   - ChatNetPacket, SquadsNetPacket
//   - ClientOnPausePacket, ClientOnResumePacket, ClientInfoNetPacket
//
// EXTRA PACKETS (extra_packets.hpp):
//   - Shop: ShopItemsRequestPacket, ShopBuyRequestPacket, etc.
//   - Clan: ClanActionRequestPacket, ClanInPacket, ClanMemberInPacket, etc.
//   - Stats: StatsRequest, GeneralStatsResponse, ScoreStatsResponse, etc.
//   - Equip: EquipRequestPacket, EquipMoveRequestPacket, SellItemRequestPacket, etc.
//   - Hangar: HangarInPacket, EquipHangarActionRequest
//   - Auction: AuctionItemsRequestPacket, AuctionBidRequestPacket, etc.
//   - Missions: MissionsActionRequestPacket, QuestsActionRequestPacket
//   - Resources: ResourcesActionRequestPacket, ResourcesInfoResponsePacket
//   - Misc: FractionChangeRequest, ChangeCredentialsRequest, JsonNetPacket
//   - Equipment data: Equipment, LaserGun, ShieldGen, SpeedGen, Drone, Ammo
//   - Event data: SpaceballEventInfo, ConvoyEventInfo, Reward, RewardItem

} // namespace dynamo
