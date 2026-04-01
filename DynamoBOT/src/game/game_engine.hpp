#pragma once

#include "core/config.hpp"
#include "game/map.hpp"
#include "game/entities.hpp"
#include "game/hero.hpp"
#include "game/resource_state.hpp"
#include "game/action_queue.hpp"
#include "packets/game_packets.hpp"
#include "packets/action_packets.hpp"

#include <memory>
#include <functional>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <mutex>
#include <optional>

namespace dynamo {

// Forward declarations
class HttpClient;
class KryoClient;
class KryoBuffer;

/**
 * @brief Engine state representing current connection/game status
 */
enum class EngineState {
    NotConnected,       // Initial state, no connection attempt
    Connecting,         // TCP connection in progress
    Authenticating,     // Waiting for auth response
    Loading,            // Auth OK, waiting for map data
    InGame,             // Fully connected and map loaded
    Disconnected,       // Disconnected after being connected
    Error               // Connection/auth error
};

inline const char* engineStateToString(EngineState state) {
    switch (state) {
        case EngineState::NotConnected: return "NotConnected";
        case EngineState::Connecting: return "Connecting";
        case EngineState::Authenticating: return "Authenticating";
        case EngineState::Loading: return "Loading";
        case EngineState::InGame: return "InGame";
        case EngineState::Disconnected: return "Disconnected";
        case EngineState::Error: return "Error";
        default: return "Unknown";
    }
}

/**
 * @brief Configuration for GameEngine
 */
struct EngineConfig {
    std::string username;
    std::string password;
    std::string serverId{"eu1"};
    std::string language{"en"};
    
    // Timing
    int tickRateHz{30};                 // Main loop frequency
    int positionUpdateIntervalMs{100};  // Ship position interpolation
    int heartbeatIntervalMs{5000};      // Connection heartbeat
    int reconnectDelayMs{10000};        // Delay before reconnect attempt
    int staleEntityTimeoutMs{3000};     // Remove entities not updated in this time
    
    // Behavior
    bool autoReconnect{true};
    bool logPackets{false};
};

/**
 * @brief GameEngine - Core game client managing connection, state, and actions
 * 
 * This is the main class that coordinates:
 * - Network connection (HTTP for login, TCP/Kryo for game)
 * - Entity management (ships, boxes, player)
 * - Action queue with throttling
 * - Position interpolation
 * 
 * Design inspired by DarkOrbit bot (GameEngine.hpp) and wupacket-main
 */
class GameEngine {
public:
    // Event callbacks
    using StateCallback = std::function<void(EngineState, EngineState)>;  // (newState, oldState)
    using ErrorCallback = std::function<void(const std::string&)>;
    using MapLoadedCallback = std::function<void(const MapInfo&)>;
    using DeathCallback = std::function<void()>;
    using ReviveCallback = std::function<void()>;
    using PacketCallback = std::function<void(int32_t packetId, const std::vector<uint8_t>&)>;
    
    GameEngine();
    ~GameEngine();
    
    // Non-copyable
    GameEngine(const GameEngine&) = delete;
    GameEngine& operator=(const GameEngine&) = delete;
    
    //--------------------------------------------------------------------------
    // Lifecycle
    //--------------------------------------------------------------------------
    
    /**
     * @brief Initialize engine with configuration
     * @param config Engine settings including credentials
     * @return true if initialization successful
     */
    bool initialize(const EngineConfig& config);
    
    /**
     * @brief Start connection and login process
     * @return true if connection started successfully
     */
    bool connect();
    
    /**
     * @brief Disconnect from server
     */
    void disconnect();
    
    /**
     * @brief Update engine - call this regularly (ideally at tickRateHz)
     * 
     * This processes:
     * - Incoming packets
     * - Position interpolation
     * - Action queue flushing
     * - Heartbeat/keepalive
     */
    void update();
    
    /**
     * @brief Shutdown engine and release resources
     */
    void shutdown();
    
    //--------------------------------------------------------------------------
    // State
    //--------------------------------------------------------------------------
    
    EngineState state() const { return state_.load(); }
    bool isConnected() const { return state_ == EngineState::InGame; }
    bool canSend() const { return state_ == EngineState::InGame && !isDead(); }
    bool isMapLoaded() const { return mapLoaded_.load(); }
    bool isDead() const { return isDead_.load(); }
    
    const std::string& lastError() const { return lastError_; }
    const EngineConfig& config() const { return config_; }
    
    //--------------------------------------------------------------------------
    // Game Data (thread-safe via snapshots)
    //--------------------------------------------------------------------------
    
    /**
     * @brief Get hero (player) state snapshot
     * Thread-safe copy of current player state
     */
    HeroSnapshot hero() const;
    
    /**
     * @brief Get entities snapshot
     * Thread-safe copy of current entity state
     */
    EntitiesSnapshot entities() const;
    
    /**
     * @brief Interpolate entity positions for smooth rendering
     * @param deltaSeconds Time since last frame
     */
    void interpolateEntities(float deltaSeconds);
    
    /**
     * @brief Get map info
     */
    MapInfo mapInfo() const;

    /**
     * @brief Get latest resource/refine/enrichment/trade snapshot
     */
    ResourceStateSnapshot resourceState() const;
    
    /**
     * @brief Get current map name
     */
    std::string currentMap() const;
    
    /**
     * @brief Get player ID
     */
    int32_t playerId() const { return playerId_; }
    
    /**
     * @brief Get session ID
     */
    const std::string& sessionId() const { return ssid_; }

    /**
     * @brief Runtime packet diagnostics
     */
    uint64_t totalPacketsReceived() const { return totalPacketsReceived_.load(); }
    uint64_t gameStatePacketsReceived() const { return gameStatePacketsReceived_.load(); }
    uint64_t packetParseErrors() const { return packetParseErrors_.load(); }
    int32_t lastPacketId() const { return lastPacketId_.load(); }
    
    //--------------------------------------------------------------------------
    // Actions
    //--------------------------------------------------------------------------
    
    /**
     * @brief Move to position
     * @param targetX Target X coordinate
     * @param targetY Target Y coordinate
     */
    void moveTo(float targetX, float targetY);
    
    /**
     * @brief Lock/select target
     * @param targetId Entity ID to lock (0 to deselect)
     */
    void lockTarget(int32_t targetId);
    
    /**
     * @brief Start attacking current target
     */
    void attack();
    
    /**
     * @brief Stop attacking
     */
    void stopAttack();
    
    /**
     * @brief Collect a collectable (box/resource)
     * @param collectableId ID of the collectable
     */
    void collect(int32_t collectableId);
    
    /**
     * @brief Switch ammo type
     * @param ammoType Ammo type ID (1-6 for lasers)
     */
    void switchAmmo(int32_t ammoType);
    
    /**
     * @brief Switch rocket type
     * @param rocketType Rocket type ID
     */
    void switchRocket(int32_t rocketType);

    /**
     * @brief Enable or disable automatic rocket firing
     * @param enabled True to enable auto-rocket, false to disable it
     */
    void setAutoRocketEnabled(bool enabled);
    
    /**
     * @brief Switch ship configuration
     * @param configIndex Config number (1 or 2)
     */
    void switchConfig(int32_t configIndex);
    
    /**
     * @brief Use ability/special item
     * @param abilityId Ability type (see UserActionType enum)
     */
    void useAbility(int32_t abilityId);
    
    /**
     * @brief Teleport through portal (must be near one)
     */
    void teleport();

    /**
     * @brief Clear all pending actions in the action queue
     */
    void clearPendingActions();
    
    /**
     * @brief Request revive/repair after death
     */
    void revive();
    
    /**
     * @brief Logout from game
     */
    void logout();

    //--------------------------------------------------------------------------
    // Resources
    //--------------------------------------------------------------------------

    /**
     * @brief Simulate opening the resources panel and fetch refine/enrichment state
     */
    void requestResourcesInfo();

    /**
     * @brief Refine a resource target (Darkonit..Xureon)
     * @param resourceType Output resource type id (4..8)
     * @param amount Output amount to refine
     */
    void refineResource(int32_t resourceType, int32_t amount);

    /**
     * @brief Enrich a module using a resource
     * @param moduleType 0=lasers, 1=rockets, 2=shields, 3=speed
     * @param resourceType Resource type id
     * @param amount Number of resource units to apply
     */
    void enrichModule(int32_t moduleType, int32_t resourceType, int32_t amount);

    /**
     * @brief Simulate opening the trade tab and fetch trade/sell state
     */
    void requestResourcesTradeInfo();

    /**
     * @brief Sell a resource stack via the trade station flow
     * @param resourceType Resource type id (0..8)
     * @param amount Amount to sell
     */
    void sellResource(int32_t resourceType, int32_t amount);

    //--------------------------------------------------------------------------
    // Shop
    //--------------------------------------------------------------------------

    /**
     * @brief Request shop item catalog from server
     */
    void requestShopItems();

    /**
     * @brief Buy an item from the shop
     * @param itemId Shop item ID
     * @param quantity Number of items to buy
     * @param price Total price
     */
    void buyShopItem(const std::string& itemId, int32_t quantity, int32_t price);

    struct ShopItem {
        std::string itemId;
        std::string title;
        int32_t price{0};
        int32_t quantity{1};
        std::string currencyKindId;
    };

    [[nodiscard]] const std::vector<ShopItem>& shopCatalog() const { return shopCatalog_; }
    [[nodiscard]] bool hasShopCatalog() const { return !shopCatalog_.empty(); }

    //--------------------------------------------------------------------------
    // Action Queue (advanced)
    //--------------------------------------------------------------------------
    
    /**
     * @brief Queue a raw action with throttling
     * @param data Packet data
     * @param delayMs Delay before sending (ms)
     * @param actionName Name for deduplication/logging
     * @param dedupeKey If set, removes previous action with same key
     * @param throttleMs Minimum time between actions with same throttle key
     */
    void queueAction(std::vector<uint8_t> data, 
                     int delayMs = 0,
                     const std::string& actionName = "",
                     const std::string& dedupeKey = "",
                     int throttleMs = 0);
    
    //--------------------------------------------------------------------------
    // Event Callbacks
    //--------------------------------------------------------------------------
    
    void onStateChange(StateCallback cb) { stateCallback_ = std::move(cb); }
    void onError(ErrorCallback cb) { errorCallback_ = std::move(cb); }
    void onMapLoaded(MapLoadedCallback cb) { mapLoadedCallback_ = std::move(cb); }
    void onDeath(DeathCallback cb) { deathCallback_ = std::move(cb); }
    void onRevive(ReviveCallback cb) { reviveCallback_ = std::move(cb); }
    void onPacket(PacketCallback cb) { packetCallback_ = std::move(cb); }
    
    //--------------------------------------------------------------------------
    // Position Prediction
    //--------------------------------------------------------------------------
    
    /**
     * @brief Predict hero position after move command
     * Used for immediate local feedback before server confirms
     */
    void predictMove(float targetX, float targetY);
    
    /**
     * @brief Get distance to entity
     * @param entityId Entity ID
     * @return Distance or -1 if entity not found
     */
    float distanceTo(int32_t entityId) const;
    
    /**
     * @brief Get distance to position
     */
    float distanceTo(float x, float y) const;
    
private:
    void setState(EngineState newState);
    void setError(const std::string& error);
    
    // Networking
    bool fetchMetaInfo();
    bool fetchLoginToken();
    bool connectTcp();
    void sendAuthPacket();
    void sendAuthFractionRequest();
    void requestMapConnect();
    void sendFrameworkKeepAlivePacket();
    void sendClientResumePacket();
    void sendClientInfoPacket(bool synchronize = false);
    
    // Packet handling
    void handlePacket(const std::vector<uint8_t>& data);
    void handleApiResponse(const std::string& uri, const std::string& jsonData);
    void handleApiNotification(const std::string& key, const std::string& jsonData);
    void handleAuthResponse(const std::string& jsonData);
    void handleMapInfoNotification(const std::string& jsonData);
    void handleGameState(KryoBuffer& buffer);
    void handleUserInfo(KryoBuffer& buffer);
    void handleGameEvent(int32_t eventId, const std::string& data);
    void updateDeathState(bool dead, const char* source, bool zeroHeroHealth = false);
    void updateHeroFraction(int32_t fraction, const char* source);
    
    // Entity management
    void updateShip(int32_t shipId,
                    const std::vector<ChangedParameter>& changes,
                    bool destroyed,
                    int32_t relation,
                    int32_t clanRelation);
    void updateCollectable(const CollectableInPacket& coll);
    void updatePlayerFromShip(int32_t shipId);
    void pruneStaleEntities();
    
    // Sending
    void sendPacket(const std::vector<uint8_t>& data);
    void sendUserAction(int32_t actionId, const std::string& data = "");
    void sendApiRequest(const std::string& uri, const std::string& jsonBody);
    void sendResourcesAction(int32_t actionId, std::optional<std::vector<int32_t>> data = std::nullopt);
    
    // Internal helpers
    std::string buildClientInfoJson() const;
    
    // State
    std::atomic<EngineState> state_{EngineState::NotConnected};
    std::atomic<bool> mapLoaded_{false};
    std::atomic<bool> isDead_{false};
    std::atomic<uint64_t> totalPacketsReceived_{0};
    std::atomic<uint64_t> gameStatePacketsReceived_{0};
    std::atomic<uint64_t> packetParseErrors_{0};
    std::atomic<int32_t> lastPacketId_{-1};
    
    EngineConfig config_;
    std::string lastError_;
    
    // Server info
    MetaInfo metaInfo_;
    LoginToken loginToken_;
    std::string serverHost_;
    int serverPort_{0};
    std::string ssid_;
    int32_t playerId_{0};
    
    // Network clients
    std::unique_ptr<HttpClient> http_;
    std::unique_ptr<KryoClient> kryo_;
    
    // Game state (protected by mutex)
    mutable std::mutex stateMutex_;
    Hero hero_;
    Entities entities_;
    MapInfo mapInfo_;
    ResourceStateSnapshot resourceState_;
    
    // Action queue
    ActionQueue actionQueue_;
    
    // Timing
    using Clock = std::chrono::steady_clock;
    Clock::time_point lastUpdate_;
    Clock::time_point lastHeartbeat_;
    Clock::time_point lastNetworkWrite_;
    Clock::time_point lastPositionUpdate_;
    Clock::time_point lastPrune_;
    
    // Local movement target hold to hide retarget flicker until server catches up
    bool hasLocalMoveTarget_{false};
    float localMoveTargetX_{0.0f};
    float localMoveTargetY_{0.0f};
    Clock::time_point localMoveTargetExpiry_{};
    
    // Request tracking
    int nextRequestId_{1};
    std::atomic<bool> authFractionRequested_{false};
    std::atomic<bool> mapConnectRequested_{false};

    // Shop
    std::vector<ShopItem> shopCatalog_;
    std::vector<int> clientVersion_{1, 233, 0};
    
    // Callbacks
    StateCallback stateCallback_;
    ErrorCallback errorCallback_;
    MapLoadedCallback mapLoadedCallback_;
    DeathCallback deathCallback_;
    ReviveCallback reviveCallback_;
    PacketCallback packetCallback_;
};

} // namespace dynamo
