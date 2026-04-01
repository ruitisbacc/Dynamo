#include "game/game_engine.hpp"
#include "network/clients/http_client.hpp"
#include "network/clients/kryo_client.hpp"
#include "network/codec/kryo_serializer.hpp"
#include "network/registry/packet_registry.hpp"
#include "bot/support/npc_database.hpp"
#include "packets/api_packets.hpp"
#include "packets/auth_packets.hpp"
#include "packets/action_packets.hpp"
#include "packets/map_packets.hpp"

#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <thread>
#include <format>
#include <algorithm>
#include <openssl/sha.h>
#include <array>

namespace dynamo
{

    using json = nlohmann::json;

    namespace
    {

        // Kryo writes class IDs as registrationId + 2 (DefaultClassResolver),
        // but registration IDs in this project enum are based on PacketHook's
        // logical order (Object=0). In the real client, Kryo pre-registers
        // primitives/FrameworkMessage classes first, so gameplay packet wire IDs
        // are shifted by 14 relative to PacketId values.
        constexpr int32_t kKryoClassIdOffset = 14;

        namespace FrameworkWireClass
        {
            constexpr int32_t RegisterTCP = 11;
            constexpr int32_t RegisterUDP = 12;
            constexpr int32_t KeepAlive = 13;
            constexpr int32_t DiscoverHost = 14;
            constexpr int32_t Ping = 15;
        }

        int32_t decodeKryoPacketId(int32_t wireClassId)
        {
            return wireClassId - kKryoClassIdOffset;
        }

        int32_t encodeKryoPacketId(PacketId id)
        {
            return static_cast<int32_t>(id) + kKryoClassIdOffset;
        }

        void appendEngineLog(const std::string &message)
        {
            (void)message;
        }

        std::vector<int> parseClientVersion(const std::string &version)
        {
            std::vector<int> result;
            std::stringstream ss(version);
            std::string part;
            while (std::getline(ss, part, '.'))
            {
                try
                {
                    result.push_back(std::stoi(part));
                }
                catch (...)
                {
                    return {1, 233, 0};
                }
            }

            if (result.size() == 3)
            {
                return result;
            }
            return {1, 233, 0};
        }

        std::string generateDeterministicUid(const std::string &input)
        {
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256(reinterpret_cast<const unsigned char *>(input.data()), input.size(), hash);

            static constexpr char hexDigits[] = "0123456789abcdef";
            std::string hex;
            hex.reserve(SHA256_DIGEST_LENGTH * 2);
            for (unsigned char byte : hash)
            {
                hex.push_back(hexDigits[(byte >> 4) & 0x0F]);
                hex.push_back(hexDigits[byte & 0x0F]);
            }

            return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
                   hex.substr(16, 4) + "-" + hex.substr(20, 12);
        }

        json buildClientInfoPayload(const EngineConfig &config, const std::vector<int> &clientVersion)
        {
            const std::string language = config.language.empty() ? "en" : config.language;
            const std::string locale = language == "en" ? "en_US" : language;

            return {
                {"uid", generateDeterministicUid(config.username)},
                {"build", 0},
                {"platform", "Desktop"},
                {"version", json(clientVersion)},
                {"systemLocale", locale},
                {"preferredLocale", language},
                {"locale", locale},
                {"language", language},
                {"clientHash", "269980fe6e943c59e8ff10338f719870"},
                {"conversionData", nullptr}};
        }

        bool isFrameworkWireClass(int32_t wireClassId)
        {
            return wireClassId >= FrameworkWireClass::RegisterTCP &&
                   wireClassId <= FrameworkWireClass::Ping;
        }

        std::string frameworkPacketName(int32_t wireClassId)
        {
            switch (wireClassId)
            {
            case FrameworkWireClass::RegisterTCP:
                return "Framework.RegisterTCP";
            case FrameworkWireClass::RegisterUDP:
                return "Framework.RegisterUDP";
            case FrameworkWireClass::KeepAlive:
                return "Framework.KeepAlive";
            case FrameworkWireClass::DiscoverHost:
                return "Framework.DiscoverHost";
            case FrameworkWireClass::Ping:
                return "Framework.Ping";
            default:
                return std::format("Framework.Unknown({})", wireClassId);
            }
        }

        bool parseIntArrayJson(const std::string &data, std::array<int32_t, 4> &outValues)
        {
            try
            {
                auto parsed = json::parse(data);
                if (parsed.is_object() && parsed.contains("state"))
                {
                    parsed = parsed["state"];
                }
                if (!parsed.is_array() || parsed.size() < outValues.size())
                {
                    return false;
                }
                for (size_t i = 0; i < outValues.size(); ++i)
                {
                    if (!parsed[i].is_number_integer())
                    {
                        return false;
                    }
                    outValues[i] = parsed[i].get<int32_t>();
                }
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool parseIntJson(const std::string &data, int32_t &outValue)
        {
            try
            {
                auto parsed = json::parse(data);
                if (parsed.is_object() && parsed.contains("state"))
                {
                    parsed = parsed["state"];
                }
                if (parsed.is_array())
                {
                    if (parsed.empty() || !parsed[0].is_number_integer())
                    {
                        return false;
                    }
                    outValue = parsed[0].get<int32_t>();
                    return true;
                }
                if (parsed.is_number_integer())
                {
                    outValue = parsed.get<int32_t>();
                    return true;
                }
                return false;
            }
            catch (...)
            {
                return false;
            }
        }

        bool hasExplicitPlayerSignals(const ShipInfo &ship)
        {
            return !ship.clanTag.empty() ||
                   ship.relation != 0 ||
                   ship.clanRelation != 0 ||
                   ship.isEnemy ||
                   ship.isAlly;
        }

        bool hasStrongPlayerIdentity(const ShipInfo &ship)
        {
            return !ship.clanTag.empty() || ship.droneCount > 0;
        }

        bool shouldClassifyShipAsNpc(const ShipInfo &ship, int32_t playerId)
        {
            if (ship.id == 0 || ship.id == playerId)
            {
                return false;
            }

            if (ship.name.empty())
            {
                return false;
            }

            if (isKnownNpc(ship.name) && !hasStrongPlayerIdentity(ship))
            {
                return true;
            }

            if (hasStrongPlayerIdentity(ship) || hasExplicitPlayerSignals(ship))
            {
                return false;
            }

            return false;
        }

        const char *classifyPlayerAffinity(const ShipInfo &ship,
                                           int32_t playerFraction,
                                           const std::string &playerClanTag)
        {
            if (ship.isNpc)
            {
                return "npc";
            }
            if (ship.isEnemy)
            {
                return "enemy";
            }
            if (ship.isAlly)
            {
                return "ally";
            }
            if (!playerClanTag.empty() && !ship.clanTag.empty() && ship.clanTag == playerClanTag)
            {
                return "ally:clan";
            }
            if (playerFraction <= 0)
            {
                return "unknown";
            }
            if (playerFraction > 0 && ship.fraction == playerFraction)
            {
                return "ally:fraction";
            }
            if (playerFraction > 0 && ship.fraction != playerFraction && ship.fraction > 0)
            {
                return "enemy:fraction";
            }
            return "enemy:fallback";
        }

    } // namespace

    //------------------------------------------------------------------------------
    // Construction/Destruction
    //------------------------------------------------------------------------------

    GameEngine::GameEngine()
    {
        lastUpdate_ = Clock::now();
        lastHeartbeat_ = Clock::now();
        lastNetworkWrite_ = Clock::now();
        lastPositionUpdate_ = Clock::now();
        lastPrune_ = Clock::now();

        // Set up action queue send callback
        actionQueue_.setSendCallback([this](const std::vector<uint8_t> &data)
                                     { sendPacket(data); });
    }

    GameEngine::~GameEngine()
    {
        shutdown();
    }

    //------------------------------------------------------------------------------
    // Lifecycle
    //------------------------------------------------------------------------------

    bool GameEngine::initialize(const EngineConfig &config)
    {
        config_ = config;
        lastError_.clear();

        // Create HTTP client
        http_ = std::make_unique<HttpClient>();

        // Create Kryo client
        kryo_ = std::make_unique<KryoClient>();

        // Set up packet handler - packets are received on IO thread,
        // delivered via poll() on main thread
        kryo_->onPacket([this](std::vector<uint8_t> data)
                        { handlePacket(data); });

        kryo_->onError([this](const std::string &error)
                       {
        std::cerr << "[KryoClient Error] " << error << std::endl;
        if (state_ == EngineState::InGame || state_ == EngineState::Loading || state_ == EngineState::Authenticating) {
            lastError_ = error;
            appendEngineLog(std::format("[KryoClient Error] {}", error));
            if (errorCallback_) {
                errorCallback_(error);
            }
            setState(EngineState::Disconnected);
        } });

        return true;
    }

    bool GameEngine::connect()
    {
        lastError_.clear();

        if (state_ != EngineState::NotConnected &&
            state_ != EngineState::Disconnected &&
            state_ != EngineState::Error)
        {
            setError("Already connecting or connected");
            return false;
        }

        setState(EngineState::Connecting);

        // Step 1: Fetch meta info (server list)
        if (!fetchMetaInfo())
        {
            return false;
        }

        // Step 2: Fetch login token
        if (!fetchLoginToken())
        {
            return false;
        }

        // Step 3: Connect TCP
        if (!connectTcp())
        {
            return false;
        }

        // Step 4: Send auth packet
        setState(EngineState::Authenticating);
        sendAuthPacket();

        return true;
    }

    void GameEngine::disconnect()
    {
        if (kryo_)
        {
            kryo_->disconnect();
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            hero_.reset();
        }
        entities_.clear();
        actionQueue_.clear();
        mapLoaded_ = false;
        isDead_ = false;
        playerId_ = 0;
        ssid_.clear();
        authFractionRequested_ = false;
        mapConnectRequested_ = false;
        hasLocalMoveTarget_ = false;
        localMoveTargetX_ = 0.0f;
        localMoveTargetY_ = 0.0f;

        setState(EngineState::Disconnected);
    }

    void GameEngine::update()
    {
        constexpr int64_t kFrameworkKeepAliveIntervalMs = 8000;

        auto now = Clock::now();
        lastUpdate_ = now;

        // Process incoming packets (delivers from IO thread to main thread)
        if (kryo_)
        {
            kryo_->poll();
        }

        if (state_ == EngineState::Authenticating ||
            state_ == EngineState::Loading ||
            state_ == EngineState::InGame)
        {
            auto keepAliveDelta = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      now - lastNetworkWrite_)
                                      .count();
            if (keepAliveDelta >= kFrameworkKeepAliveIntervalMs)
            {
                sendFrameworkKeepAlivePacket();
            }
        }

        // Only do game updates if in-game
        if (state_ != EngineState::InGame)
        {
            return;
        }

        // Interpolate positions every frame for smooth rendering/movement
        float deltaSeconds = std::chrono::duration<float>(now - lastPositionUpdate_).count();
        if (deltaSeconds > 0.0f)
        {
            if (deltaSeconds > 0.1f)
            {
                deltaSeconds = 0.016f;
            }
            interpolateEntities(deltaSeconds);
            lastPositionUpdate_ = now;
        }

        // Prune stale entities
        auto pruneDelta = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - lastPrune_)
                              .count();
        if (pruneDelta >= 1000)
        { // Prune every second
            pruneStaleEntities();
            lastPrune_ = now;
        }

        // Flush action queue
        actionQueue_.flush();

        // Heartbeat / ClientInfo (send periodically to keep connection alive)
        auto heartbeatDelta = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now - lastHeartbeat_)
                                  .count();
        if (heartbeatDelta >= config_.heartbeatIntervalMs)
        {
            sendClientInfoPacket(false);
            lastHeartbeat_ = Clock::now();
        }
    }

    void GameEngine::shutdown()
    {
        disconnect();
        http_.reset();
        kryo_.reset();
        setState(EngineState::NotConnected);
    }

    //------------------------------------------------------------------------------
    // State
    //------------------------------------------------------------------------------

    void GameEngine::setState(EngineState newState)
    {
        EngineState oldState = state_.exchange(newState);
        appendEngineLog(std::format("[State] {} -> {}", engineStateToString(oldState), engineStateToString(newState)));

        if (oldState != newState)
        {
            if (newState == EngineState::Loading)
            {
                sendAuthFractionRequest();
            }
            else if (newState == EngineState::InGame)
            {
                sendClientResumePacket();
                sendClientInfoPacket(false);
                lastHeartbeat_ = Clock::now();
            }
        }

        if (oldState != newState && stateCallback_)
        {
            stateCallback_(newState, oldState);
        }
    }

    void GameEngine::setError(const std::string &error)
    {
        lastError_ = error;
        appendEngineLog(std::format("[Error] {}", error));
        setState(EngineState::Error);
        if (errorCallback_)
        {
            errorCallback_(error);
        }
    }

    void GameEngine::updateDeathState(bool dead, const char *source, bool zeroHeroHealth)
    {
        const bool changed = isDead_.exchange(dead) != dead;

        if (dead && zeroHeroHealth)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            hero_.health = 0;
        }

        if (!changed)
        {
            return;
        }

        appendEngineLog(std::format(
            "[Engine] Hero {} via {}",
            dead ? "destroyed" : "revived",
            source != nullptr ? source : "unknown"));

        if (dead)
        {
            if (deathCallback_)
            {
                deathCallback_();
            }
        }
        else if (reviveCallback_)
        {
            reviveCallback_();
        }
    }

    //------------------------------------------------------------------------------
    // Game Data Access
    //------------------------------------------------------------------------------

    HeroSnapshot GameEngine::hero() const
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return HeroSnapshot::from(hero_);
    }

    EntitiesSnapshot GameEngine::entities() const
    {
        int32_t playerFraction = 0;
        std::string playerClanTag;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            playerFraction = hero_.fraction;
            playerClanTag = hero_.clanTag;
        }
        return entities_.snapshot(playerId_, playerFraction, std::move(playerClanTag));
    }

    void GameEngine::interpolateEntities(float deltaSeconds)
    {
        if (deltaSeconds <= 0.0f)
        {
            return;
        }
        if (deltaSeconds > 0.1f)
        {
            deltaSeconds = 0.016f;
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            hero_.interpolatePosition(deltaSeconds);
        }

        entities_.interpolatePositions(deltaSeconds);
    }

    MapInfo GameEngine::mapInfo() const
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return mapInfo_;
    }

    std::string GameEngine::currentMap() const
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return mapInfo_.name;
    }

    float GameEngine::distanceTo(int32_t entityId) const
    {
        auto ship = entities_.getShip(entityId);
        if (ship)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            return hero_.distanceTo(ship->x, ship->y);
        }
        auto box = entities_.getBox(entityId);
        if (box)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            return hero_.distanceTo(box->x, box->y);
        }
        return -1.0f;
    }

    float GameEngine::distanceTo(float x, float y) const
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        return hero_.distanceTo(x, y);
    }

    //------------------------------------------------------------------------------
    // Actions
    //------------------------------------------------------------------------------

    void GameEngine::moveTo(float targetX, float targetY)
    {
        if (!canSend())
            return;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            hasLocalMoveTarget_ = true;
            localMoveTargetX_ = targetX;
            localMoveTargetY_ = targetY;
            localMoveTargetExpiry_ = Clock::now() + std::chrono::milliseconds(700);
        }

        // Predict movement locally
        predictMove(targetX, targetY);

        // Send move action - protocol expects "x|y"
        std::string data = std::to_string(static_cast<int>(targetX)) + "|" +
                           std::to_string(static_cast<int>(targetY));
        if (config_.logPackets)
        {
            appendEngineLog(std::format("[Move] Sending move target=({}, {}) data='{}'",
                                        static_cast<int>(targetX), static_cast<int>(targetY), data));
        }
        sendUserAction(ActionType::MOVE, data);
    }

    void GameEngine::lockTarget(int32_t targetId)
    {
        if (!canSend())
            return;
        sendUserAction(ActionType::LOCK, std::to_string(targetId));
    }

    void GameEngine::attack()
    {
        if (!canSend())
            return;
        sendUserAction(ActionType::ATTACK);
    }

    void GameEngine::stopAttack()
    {
        if (!canSend())
            return;
        sendUserAction(ActionType::STOP_ATTACK);
    }

    void GameEngine::collect(int32_t collectableId)
    {
        if (!canSend())
            return;

        CollectableCollectRequest packet(collectableId);
        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::CollectableCollectRequest), true);
        packet.serialize(buffer);

        queueAction(buffer.data(), 0, "collect", "", 200);
    }

    void GameEngine::switchAmmo(int32_t ammoType)
    {
        if (!canSend())
            return;
        if (ammoType < 1 || ammoType > 6)
            return;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            hero_.currentAmmoType = ammoType;
        }

        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::UserActionsPacket), true);
        UserActionsPacket packet(UserAction(ActionType::SELECT_LASER, std::to_string(ammoType)));
        packet.serialize(buffer);

        queueAction(buffer.data(), 0, "switch_laser", "switch_laser", 250);
    }

    void GameEngine::switchRocket(int32_t rocketType)
    {
        if (!canSend())
            return;
        if (rocketType < 1 || rocketType > 3)
            return;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            hero_.currentRocketType = rocketType;
        }

        RocketSwitchRequest packet(rocketType);
        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::RocketSwitchRequest), true);
        packet.serialize(buffer);

        queueAction(buffer.data(), 0, "switch_rocket", "switch_rocket", 500);
    }

    void GameEngine::setAutoRocketEnabled(bool enabled)
    {
        if (!canSend())
            return;

        AutoRocketRequest packet(enabled);
        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::AutoRocketRequest), true);
        packet.serialize(buffer);

        queueAction(
            buffer.data(),
            0,
            enabled ? "auto_rocket_on" : "auto_rocket_off",
            "auto_rocket",
            300);
    }

    void GameEngine::switchConfig(int32_t configIndex)
    {
        if (!canSend())
            return;
        if (configIndex != 1 && configIndex != 2)
            return;
        // WarUniverse expects actionId=5 without payload.
        sendUserAction(ActionType::SWITCH_CONFI);
    }

    void GameEngine::useAbility(int32_t abilityId)
    {
        if (!canSend())
            return;
        sendUserAction(abilityId);
    }

    void GameEngine::teleport()
    {
        if (!canSend())
            return;
        sendUserAction(ActionType::TELEPORT);
    }

    void GameEngine::clearPendingActions()
    {
        actionQueue_.clear();
        actionQueue_.resetAllThrottles();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            hasLocalMoveTarget_ = false;
            hero_.isMoving = false;
        }
    }

    void GameEngine::revive()
    {
        const auto state = state_.load();
        if (state != EngineState::InGame)
        {
            appendEngineLog(std::format(
                "[Engine] Skipping RepairRequestPacket: state={}",
                engineStateToString(state)));
            return;
        }

        // Send RepairRequestPacket (empty packet in protocol)
        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::RepairRequestPacket), true);
        appendEngineLog("[Engine] Sending RepairRequestPacket");

        sendPacket(buffer.data());
    }

    void GameEngine::logout()
    {
        if (state_ != EngineState::InGame)
            return;
        sendUserAction(ActionType::LOGOUT);
    }

    void GameEngine::predictMove(float targetX, float targetY)
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        hero_.targetX = targetX;
        hero_.targetY = targetY;
        hero_.isMoving = true;
    }

    void GameEngine::queueAction(std::vector<uint8_t> data,
                                 int delayMs,
                                 const std::string &actionName,
                                 const std::string &dedupeKey,
                                 int throttleMs)
    {
        actionQueue_.enqueue(std::move(data), delayMs, actionName, dedupeKey, throttleMs);
    }

    //------------------------------------------------------------------------------
    // Networking (private)
    //------------------------------------------------------------------------------

    bool GameEngine::fetchMetaInfo()
    {
        std::string region = config_.serverId.substr(0, 2); // e.g., "eu" from "eu1"

        auto response = http_->fetchMetaInfo(region);
        if (!response)
        {
            setError("Failed to fetch meta info: " + http_->lastError());
            return false;
        }

        metaInfo_ = *response;
        clientVersion_ = parseClientVersion(metaInfo_.lastClientVersion);

        // Find our game server
        auto server = metaInfo_.getServer(config_.serverId);
        if (!server)
        {
            setError("Server not found: " + config_.serverId);
            return false;
        }
        serverHost_ = server->host;
        serverPort_ = server->port;

        // Find login server
        auto loginServer = metaInfo_.getLoginServer(config_.serverId);
        if (!loginServer)
        {
            setError("Login server not found for: " + config_.serverId);
            return false;
        }
        metaInfo_.loginServerUrl = loginServer->baseUrl;

        if (config_.logPackets)
        {
            std::cout << std::format("[Engine] Server: {}:{}\n", serverHost_, serverPort_);
            std::cout << std::format("[Engine] Login URL: {}\n", metaInfo_.loginServerUrl);
            appendEngineLog(std::format("[Engine] Server: {}:{}", serverHost_, serverPort_));
            appendEngineLog(std::format("[Engine] Login URL: {}", metaInfo_.loginServerUrl));
        }

        return true;
    }

    bool GameEngine::fetchLoginToken()
    {
        auto tokenResult = http_->getLoginToken(
            metaInfo_.loginServerUrl, config_.username, config_.password);

        if (!tokenResult)
        {
            setError("Login failed: " + http_->lastError());
            return false;
        }

        loginToken_ = *tokenResult;

        if (config_.logPackets)
        {
            std::cout << "[Engine] Login token acquired\n";
            appendEngineLog("[Engine] Login token acquired");
        }

        return true;
    }

    bool GameEngine::connectTcp()
    {
        if (!kryo_->connect(serverHost_, serverPort_))
        {
            setError("Failed to connect to game server: " + kryo_->lastError());
            return false;
        }

        if (config_.logPackets)
        {
            std::cout << std::format("[Engine] TCP connected to {}:{}\n", serverHost_, serverPort_);
            appendEngineLog(std::format("[Engine] TCP connected to {}:{}", serverHost_, serverPort_));
        }

        return true;
    }

    void GameEngine::sendAuthPacket()
    {
        ssid_.clear();
        authFractionRequested_ = false;
        mapConnectRequested_ = false;

        // Build auth token
        std::string combinedToken = loginToken_.combined();

        if (config_.logPackets)
        {
            appendEngineLog(std::format("[Auth] tokenId='{}' token='{}'",
                                        loginToken_.tokenId, loginToken_.token.substr(0, 20) + "..."));
            appendEngineLog(std::format("[Auth] combined='{}'",
                                        combinedToken.substr(0, 50) + (combinedToken.size() > 50 ? "..." : "")));
        }

        // Keep token-login clientInfo aligned with periodic ClientInfoNetPacket payloads.
        json clientInfo = buildClientInfoPayload(config_, clientVersion_);

        // Build request JSON
        json requestBody = {
            {"token", combinedToken},
            {"clientInfo", clientInfo}};

        // Send as ApiRequestPacket
        sendApiRequest(WuApiUri::TOKEN_LOGIN, requestBody.dump());
    }

    void GameEngine::requestMapConnect()
    {
        if (ssid_.empty())
        {
            if (config_.logPackets)
            {
                appendEngineLog("[Engine] Skipping MapConnectRequestPacket: ssid is empty");
            }
            return;
        }

        if (mapConnectRequested_.exchange(true))
        {
            return;
        }

        MapConnectRequestPacket packet;
        packet.ssid = ssid_;
        packet.lang = config_.language.empty() ? "en" : config_.language;

        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::MapConnectRequestPacket), true);
        packet.serialize(buffer);

        if (config_.logPackets)
        {
            appendEngineLog(std::format(
                "[Engine] Sending MapConnectRequestPacket ssidLen={} lang={}",
                packet.ssid.size(), packet.lang));
        }

        sendPacket(buffer.data());
    }

    void GameEngine::handlePacket(const std::vector<uint8_t> &data)
    {
        if (data.empty())
            return;

        KryoBuffer buffer{std::vector<uint8_t>(data)};
        int32_t wireClassId = buffer.readVarInt(true);
        const bool frameworkPacket = isFrameworkWireClass(wireClassId);
        int32_t packetId = frameworkPacket ? -wireClassId : decodeKryoPacketId(wireClassId);
        lastPacketId_.store(packetId);
        totalPacketsReceived_.fetch_add(1);

        if (config_.logPackets)
        {
            const auto packetName = frameworkPacket
                                        ? frameworkPacketName(wireClassId)
                                        : PacketRegistry::getPacketName(static_cast<PacketId>(packetId));
            std::cout << std::format("[Packet] wire={} id={} ({}) size: {}\n",
                                     wireClassId, packetId, packetName, data.size());
            appendEngineLog(std::format("[Packet] wire={} id={} ({}) size: {}",
                                        wireClassId, packetId, packetName, data.size()));

            // Hex dump first 64 bytes for debugging auth and unknown packets
            if (packetId == static_cast<int32_t>(PacketId::GameStateResponsePacket))
            {
                gameStatePacketsReceived_.fetch_add(1);
            }

            // Always hex dump first 8192 bytes for debugging
            std::cout << "[HexDump] ";
            std::string hexDump = "[HexDump] ";
            size_t dumpLen = std::min(data.size(), size_t{8192});
            for (size_t i = 0; i < dumpLen; ++i)
            {
                std::cout << std::format("{:02X} ", data[i]);
                hexDump += std::format("{:02X} ", data[i]);
            }
            std::cout << (data.size() > 8192 ? "...\n" : "\n");
            if (data.size() > 8192)
            {
                hexDump += "...";
            }
            appendEngineLog(hexDump);
        }

        // Notify raw packet callback
        if (packetCallback_)
        {
            packetCallback_(packetId, data);
        }

        try
        {
            if (frameworkPacket)
            {
                switch (wireClassId)
                {
                case FrameworkWireClass::RegisterTCP:
                {
                    const int32_t connectionId = buffer.readKryoInt();
                    if (config_.logPackets)
                    {
                        appendEngineLog(std::format("[Framework] RegisterTCP connectionId={}", connectionId));
                    }
                    return;
                }

                case FrameworkWireClass::RegisterUDP:
                {
                    const int32_t connectionId = buffer.readKryoInt();
                    if (config_.logPackets)
                    {
                        appendEngineLog(std::format("[Framework] RegisterUDP connectionId={}", connectionId));
                    }
                    return;
                }

                case FrameworkWireClass::KeepAlive:
                    if (config_.logPackets)
                    {
                        appendEngineLog("[Framework] KeepAlive");
                    }
                    return;

                case FrameworkWireClass::DiscoverHost:
                    if (config_.logPackets)
                    {
                        appendEngineLog("[Framework] DiscoverHost");
                    }
                    return;

                case FrameworkWireClass::Ping:
                {
                    const int32_t pingId = buffer.readKryoInt();
                    const bool isReply = buffer.readBool();
                    if (config_.logPackets)
                    {
                        appendEngineLog(std::format("[Framework] Ping id={} reply={}", pingId, isReply));
                    }

                    if (!isReply && kryo_)
                    {
                        KryoBuffer reply;
                        reply.writeVarInt(FrameworkWireClass::Ping, true);
                        reply.writeKryoInt(pingId);
                        reply.writeBool(true);
                        sendPacket(reply.data());
                    }
                    return;
                }

                default:
                    return;
                }
            }

            switch (static_cast<PacketId>(packetId))
            {
            case PacketId::ApiResponsePacket:
            {
                ApiResponsePacket response;
                response.deserialize(buffer);
                if (config_.logPackets)
                {
                    appendEngineLog(std::format(
                        "[ApiResponse] uri='{}' requestId={} infoLen={} dataLen={}",
                        response.uri, response.requestId,
                        response.responseInfoJson.size(),
                        response.responseDataJson.size()));
                }
                handleApiResponse(response.uri, response.responseDataJson);
                break;
            }

            case PacketId::ApiResponseNetStatus:
            {
                ApiResponseNetStatus status;
                status.deserialize(buffer);
                if (config_.logPackets)
                {
                    appendEngineLog(std::format(
                        "[ApiStatus] uri='{}' requestId={} status={}",
                        status.uri, status.requestId, status.status));
                }
                break;
            }

            case PacketId::ApiNotification:
            {
                ApiNotification notif;
                notif.deserialize(buffer);
                if (config_.logPackets)
                {
                    std::cout << std::format("[Notification] key='{}' payload={} bytes\n",
                                             notif.key, notif.notificationJsonString.size());
                    appendEngineLog(std::format("[Notification] key='{}' payload={} bytes",
                                                notif.key, notif.notificationJsonString.size()));
                }
                handleApiNotification(notif.key, notif.notificationJsonString);
                break;
            }

            case PacketId::AuthAnswerPacket:
            {
                AuthAnswerPacket response;
                response.deserialize(buffer);
                if (response.success)
                {
                    if (!response.ssid.empty())
                    {
                        ssid_ = response.ssid;
                    }

                    if (state_ == EngineState::Authenticating)
                    {
                        setState(EngineState::Loading);
                    }

                    if (!ssid_.empty())
                    {
                        requestMapConnect();
                    }
                    else if (config_.logPackets)
                    {
                        appendEngineLog("[Auth] AuthAnswerPacket success without ssid, waiting for later session data");
                    }
                }
                else
                {
                    setError("Auth failed: " + (response.errorMsg.empty() ? "unknown" : response.errorMsg));
                }
                break;
            }

            case PacketId::SignUpResponsePacket:
            {
                // Token-login pre-auth acknowledgement (no ssid in this packet).
                // Wait for AuthAnswerPacket or ApiResponsePacket with ssid.
                SignUpResponsePacket response;
                response.deserialize(buffer);

                if (response.success)
                {
                    if (state_ == EngineState::Authenticating)
                    {
                        setState(EngineState::Loading);
                    }
                    appendEngineLog("[Auth] SignUpResponsePacket success, waiting for ssid");
                }
                else
                {
                    setError(response.errorMsg.empty() ? "Auth failed (SignUpResponsePacket=false)"
                                                       : "Auth failed: " + response.errorMsg);
                }
                break;
            }

            case PacketId::AuthFractionAnswerPacket:
            {
                AuthFractionAnswerPacket response;
                response.deserialize(buffer);
                if (config_.logPackets)
                {
                    appendEngineLog(std::format("[Auth] Fraction={}", response.fraction));
                }
                updateHeroFraction(response.fraction, "auth-fraction");
                break;
            }

            case PacketId::MapConnectAnswerPacket:
            {
                MapConnectAnswerPacket response;
                response.deserialize(buffer);
                if (response.success)
                {
                    setState(EngineState::InGame);
                    mapLoaded_ = true;
                }
                else
                {
                    setError("Map connect failed");
                }
                break;
            }

            case PacketId::MapInfoPacket:
            {
                MapInfoPacket mapPacket;
                mapPacket.deserialize(buffer);

                MapInfo callbackMapInfo;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    mapInfo_.id = mapPacket.mapId;
                    mapInfo_.name = mapPacket.name;
                    mapInfo_.width = mapPacket.width;
                    mapInfo_.height = mapPacket.height;
                    // mapInfo_.fraction = ... (not in packet)
                    // mapInfo_.pvp = ... (not in packet)

                    // Update portals from map packet
                    entities_.clearPortals();
                    for (size_t i = 0; i < mapPacket.teleports.size(); ++i)
                    {
                        const auto &tp = mapPacket.teleports[i];
                        PortalInfo portal;
                        portal.id = static_cast<int32_t>(i + 1); // Use index as ID since packet lacks it
                        portal.x = static_cast<float>(tp.x);
                        portal.y = static_cast<float>(tp.y);
                        portal.targetMapId = 0; // targetMapId not in this packet version
                        portal.active = true;
                        entities_.updatePortal(portal.id, portal);
                    }

                    mapLoaded_ = true;
                    callbackMapInfo = mapInfo_;
                }

                if (mapLoadedCallback_)
                {
                    mapLoadedCallback_(callbackMapInfo);
                }
                break;
            }
            case PacketId::GameStateResponsePacket:
            {
                handleGameState(buffer);
                break;
            }

            case PacketId::UserInfoResponsePacket:
            {
                handleUserInfo(buffer);
                break;
            }

            case PacketId::MessageResponsePacket:
            {
                MessageResponsePacket msg;
                msg.deserialize(buffer);
                if (config_.logPackets)
                {
                    std::cout << std::format("[Message] id={}: {}\n", msg.msgId, msg.msg);
                    appendEngineLog(std::format("[Message] id={}: {}", msg.msgId, msg.msg));
                }
                break;
            }

            case PacketId::EventResponsePacket:
            {
                EventResponsePacket evt;
                evt.deserialize(buffer);
                handleGameEvent(evt.eventId, evt.dataJson);
                break;
            }

            case PacketId::GameEvent:
            {
                GameEvent event;
                event.deserialize(buffer);
                handleGameEvent(event.id, event.dataJson);
                break;
            }

            case PacketId::RepairResponsePacket:
            {
                RepairResponsePacket rep;
                rep.deserialize(buffer);
                if (rep.status == 0)
                {
                    updateDeathState(false, "repair-response");
                }
                break;
            }

            case PacketId::TeleportResponsePacket:
            {
                TeleportResponsePacket tp;
                tp.deserialize(buffer);
                // Map change - clear old entities
                if (tp.success)
                {
                    entities_.clear();
                }
                break;
            }

            default:
                // Unknown or unhandled packet
                break;
            }
        }
        catch (const std::exception &e)
        {
            packetParseErrors_.fetch_add(1);
            std::cerr << std::format("[Packet Error] ID {}: {}\n", packetId, e.what());
            appendEngineLog(std::format("[Packet Error] ID {}: {}", packetId, e.what()));
        }
    }

    void GameEngine::handleApiResponse(const std::string &uri, const std::string &jsonData)
    {
        try
        {
            if (uri == WuApiUri::TOKEN_LOGIN)
            {
                handleAuthResponse(jsonData);
            }
            else if (uri == WuApiUri::MAP_CONNECT)
            {
                // Map connect response - may contain initial data
                auto j = json::parse(jsonData);
                if (j.contains("ssid"))
                {
                    ssid_ = j["ssid"].get<std::string>();
                }
                if (j.contains("playerId"))
                {
                    playerId_ = j["playerId"].get<int32_t>();
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    hero_.id = playerId_;
                }

                setState(EngineState::InGame);
                mapLoaded_ = true;

                if (config_.logPackets)
                {
                    std::cout << std::format("[Engine] Map connected, playerId={}\n", playerId_);
                    appendEngineLog(std::format("[Engine] Map connected, playerId={}", playerId_));
                }
            }
            else if (uri == WuApiUri::PROFILE_INFO)
            {
                // Profile info response with player data
                auto j = json::parse(jsonData);
                std::optional<int32_t> profileFraction;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    if (j.contains("name"))
                        hero_.name = j["name"].get<std::string>();
                    if (j.contains("level"))
                        hero_.level = j["level"].get<int32_t>();
                    if (j.contains("fraction"))
                        profileFraction = j["fraction"].get<int32_t>();
                }
                if (profileFraction.has_value())
                {
                    if (config_.logPackets)
                    {
                        appendEngineLog(std::format("[ProfileInfo] Fraction={}", *profileFraction));
                    }
                    updateHeroFraction(*profileFraction, "profile-info");
                }
            }
            else if (uri == WuApiUri::SHOP_ITEMS)
            {
                auto j = json::parse(jsonData);
                shopCatalog_.clear();
                if (j.contains("itemsDataList") && j["itemsDataList"].is_array())
                {
                    for (const auto &item : j["itemsDataList"])
                    {
                        ShopItem si;
                        // itemId can be string or number depending on server version
                        if (item.contains("itemId"))
                        {
                            if (item["itemId"].is_string())
                            {
                                si.itemId = item["itemId"].get<std::string>();
                            }
                            else
                            {
                                si.itemId = std::to_string(item["itemId"].get<int64_t>());
                            }
                        }
                        si.title = item.value("title", "");
                        si.price = item.value("price", 0);
                        si.quantity = item.value("quantity", 1);
                        si.currencyKindId = item.value("currencyKindId", "");
                        if (!si.itemId.empty())
                        {
                            shopCatalog_.push_back(std::move(si));
                        }
                    }
                }
                appendEngineLog(std::format("[Shop] Loaded {} items from catalog", shopCatalog_.size()));
                for (const auto &si : shopCatalog_)
                {
                    appendEngineLog(std::format("[Shop]   {} id={} price={} qty={} currency={}",
                                                si.title, si.itemId, si.price, si.quantity, si.currencyKindId));
                }
            }
            else if (uri == WuApiUri::SHOP_BUY)
            {
                appendEngineLog(std::format("[Shop] Buy response: {}", jsonData));
                auto j = json::parse(jsonData);
                // status: "NORMAL" or "SUCCESS" = ok, "ERROR" = fail
                bool success = false;
                if (j.contains("status"))
                {
                    if (j["status"].is_string())
                    {
                        auto s = j["status"].get<std::string>();
                        success = (s == "NORMAL" || s == "SUCCESS" || s == "SUCCESSFUL");
                    }
                    else
                    {
                        success = j["status"].get<int>() == 0;
                    }
                }
                if (success)
                {
                    appendEngineLog("[Shop] Purchase successful!");
                }
                else
                {
                    appendEngineLog(std::format("[Shop] Purchase failed: {}", jsonData));
                }
            }
            // Other API responses can be added here
        }
        catch (const std::exception &e)
        {
            if (config_.logPackets)
            {
                std::cerr << std::format("[API Error] uri={}: {}\n", uri, e.what());
                appendEngineLog(std::format("[API Error] uri={}: {}", uri, e.what()));
            }
        }
    }

    void GameEngine::handleApiNotification(const std::string &key, const std::string &jsonData)
    {
        try
        {
            if (key == "map-info" || key == WuApiUri::MAP_INFO)
            {
                handleMapInfoNotification(jsonData);
            }
            else if (key == "player-info")
            {
                // Player info notification
                auto j = json::parse(jsonData);
                std::optional<int32_t> playerFraction;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    if (j.contains("name"))
                        hero_.name = j["name"].get<std::string>();
                    if (j.contains("level"))
                        hero_.level = j["level"].get<int32_t>();
                    if (j.contains("fraction"))
                        playerFraction = j["fraction"].get<int32_t>();
                    if (j.contains("btc"))
                        hero_.btc = j["btc"].get<int64_t>();
                    if (j.contains("plt"))
                        hero_.plt = j["plt"].get<int64_t>();
                }
                if (playerFraction.has_value())
                {
                    if (config_.logPackets)
                    {
                        appendEngineLog(std::format("[PlayerInfo] Fraction={}", *playerFraction));
                    }
                    updateHeroFraction(*playerFraction, "player-info");
                }
            }
            else if (key == "ship-destroyed")
            {
                updateDeathState(true, "api-notification", true);
            }
            else if (key == "ship-revived")
            {
                updateDeathState(false, "api-notification");
            }
            // Other notifications...
        }
        catch (const std::exception &e)
        {
            if (config_.logPackets)
            {
                std::cerr << std::format("[Notification Error] key={}: {}\n", key, e.what());
                appendEngineLog(std::format("[Notification Error] key={}: {}", key, e.what()));
            }
        }
    }

    void GameEngine::handleAuthResponse(const std::string &jsonData)
    {
        try
        {
            if (config_.logPackets)
            {
                appendEngineLog(std::format("[Auth] JSON response: {}",
                                            jsonData.substr(0, std::min(size_t{200}, jsonData.size()))));
            }

            auto j = json::parse(jsonData);

            // Check for error in various formats
            if (j.contains("error"))
            {
                setError("Auth failed: " + j["error"].get<std::string>());
                return;
            }
            if (j.contains("status") && j["status"].get<std::string>() == "ERROR")
            {
                std::string msg = j.contains("message") ? j["message"].get<std::string>() : "Unknown error";
                setError("Auth failed: " + msg);
                return;
            }

            std::string status;
            if (j.contains("status") && j["status"].is_string())
            {
                status = j["status"].get<std::string>();
            }

            if (j.contains("ssid"))
            {
                ssid_ = j["ssid"].get<std::string>();
            }
            if (j.contains("playerId"))
            {
                playerId_ = j["playerId"].get<int32_t>();
                std::lock_guard<std::mutex> lock(stateMutex_);
                hero_.id = playerId_;
            }

            if (state_ == EngineState::Authenticating)
            {
                setState(EngineState::Loading);
            }

            if (!ssid_.empty())
            {
                if (config_.logPackets)
                {
                    std::cout << std::format("[Engine] Auth session ready, ssid={}, playerId={}\n", ssid_, playerId_);
                    appendEngineLog(std::format("[Engine] Auth session ready, ssid={}, playerId={}", ssid_, playerId_));
                }
                requestMapConnect();
            }
            else if (config_.logPackets)
            {
                appendEngineLog(std::format(
                    "[Auth] Waiting for ssid after token-login status='{}' playerId={}",
                    status.empty() ? "n/a" : status, playerId_));
            }
        }
        catch (const std::exception &e)
        {
            setError("Failed to parse auth response: " + std::string(e.what()));
        }
    }

    void GameEngine::handleMapInfoNotification(const std::string &jsonData)
    {
        try
        {
            auto j = json::parse(jsonData);

            MapInfo callbackMapInfo;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);

                if (j.contains("id"))
                    mapInfo_.id = j["id"].get<int32_t>();
                if (j.contains("name"))
                    mapInfo_.name = j["name"].get<std::string>();
                if (j.contains("width"))
                    mapInfo_.width = j["width"].get<int32_t>();
                if (j.contains("height"))
                    mapInfo_.height = j["height"].get<int32_t>();
                if (j.contains("factionId"))
                    mapInfo_.fraction = j["factionId"].get<int32_t>();
                if (j.contains("fraction"))
                    mapInfo_.fraction = j["fraction"].get<int32_t>();
                if (j.contains("pvp"))
                    mapInfo_.pvp = j["pvp"].get<bool>();

                // Reset static map objects before repopulation
                mapInfo_.teleporters.clear();
                entities_.clearPortals();
                entities_.clearStations();
                entities_.clearConvoys();
                int32_t fallbackPortalId = 1000000;
                int32_t fallbackStationId = 2000000;

                // Parse teleporters/portals
                if (j.contains("teleporters"))
                {
                    for (const auto &tp : j["teleporters"])
                    {
                        MapInfo::Teleporter tele;
                        tele.id = tp.value("id", 0);
                        if (tele.id == 0)
                        {
                            tele.id = fallbackPortalId++;
                        }
                        tele.x = tp.value("x", 0.0f);
                        tele.y = tp.value("y", 0.0f);
                        tele.targetMapId = tp.value("targetMapId", 0);
                        tele.targetX = tp.value("targetX", 0.0f);
                        tele.targetY = tp.value("targetY", 0.0f);
                        tele.active = tp.value("active", true);
                        mapInfo_.teleporters.push_back(tele);

                        PortalInfo portal;
                        portal.id = tele.id;
                        portal.x = tele.x;
                        portal.y = tele.y;
                        portal.targetMapId = tele.targetMapId;
                        portal.type = "TELEPORTER";
                        portal.active = tele.active;
                        entities_.updatePortal(portal.id, portal);
                    }
                }

                // Parse mapObjects (alternative format with NORMAL_TELEPORT, etc.)
                if (j.contains("mapObjects"))
                {
                    for (const auto &obj : j["mapObjects"])
                    {
                        std::string objType = obj.value("type", "");
                        // Handle various teleport type names
                        if (objType == "portal" || objType == "teleporter" ||
                            objType == "NORMAL_TELEPORT" || objType == "TELEPORT" ||
                            objType.find("TELEPORT") != std::string::npos)
                        {
                            PortalInfo portal;
                            portal.id = obj.value("id", 0);
                            if (portal.id == 0)
                            {
                                portal.id = fallbackPortalId++;
                            }
                            portal.x = obj.value("x", 0.0f);
                            portal.y = obj.value("y", 0.0f);
                            portal.targetMapId = obj.value("targetMapId", 0);
                            portal.targetMapName = obj.value("targetMapName", "");
                            portal.type = objType;
                            portal.active = obj.value("active", true);
                            entities_.updatePortal(portal.id, portal);
                        }
                        // Handle station types (SPACE_STATION, TRADE_STATION, QUEST_STATION, etc.)
                        else if (objType.find("STATION") != std::string::npos)
                        {
                            StationInfo station;
                            station.id = obj.value("id", 0);
                            if (station.id == 0)
                            {
                                station.id = fallbackStationId++;
                            }
                            station.x = obj.value("x", 0.0f);
                            station.y = obj.value("y", 0.0f);
                            station.type = objType;
                            station.subtype = obj.value("subtype", 0);
                            station.name = obj.value("name", objType);
                            entities_.updateStation(station.id, station);
                        }
                    }
                }

                mapLoaded_ = true;
                callbackMapInfo = mapInfo_;
            }

            if (state_ == EngineState::Loading)
            {
                setState(EngineState::InGame);
            }

            if (mapLoadedCallback_)
            {
                mapLoadedCallback_(callbackMapInfo);
            }

            if (config_.logPackets)
            {
                std::cout << std::format("[Engine] Map loaded: {} ({}x{})\n",
                                         callbackMapInfo.name, callbackMapInfo.width, callbackMapInfo.height);
                appendEngineLog(std::format("[Engine] Map loaded: {} ({}x{})",
                                            callbackMapInfo.name, callbackMapInfo.width, callbackMapInfo.height));
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << std::format("[Engine] Failed to parse map info: {}\n", e.what());
            appendEngineLog(std::format("[Engine] Failed to parse map info: {}", e.what()));
        }
    }

    void GameEngine::handleGameState(KryoBuffer &buffer)
    {
        // GameStateResponsePacket format (Kryo FieldSerializer, alphabetical fields):
        // 1. collectables (CollectableInPacket[])
        // 2. confi (int)
        // 3. events (GameEvent[])
        // 4. flushCollectables (boolean)
        // 5. mapChanges (ChangedParameter[]) - deprecated
        // 6. mapEvents (MapEvent[])
        // 7. playerId (int)
        // 8. safeZone (boolean)
        // 9. ships (ShipInResponse[])

        try
        {
            GameStateResponsePacket packet;
            packet.deserialize(buffer);

            // 1. collectables
            for (const auto &coll : packet.collectables)
            {
                updateCollectable(coll);
            }

            // 2. confi
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                hero_.activeConfig = packet.confi;
            }

            // 3. events
            for (const auto &event : packet.events)
            {
                handleGameEvent(event.id, event.dataJson);
            }

            // 4. flushCollectables
            if (packet.flushCollectables)
            {
                // Server is resending full list - but updateCollectable handles it.
            }

            // 6. mapEvents
            // (currently not used)

            // 7. playerId
            if (playerId_ == 0 && packet.playerId > 0)
            {
                playerId_ = packet.playerId;
                std::lock_guard<std::mutex> lock(stateMutex_);
                hero_.id = playerId_;
            }

            // 8. safeZone
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                hero_.inSafeZone = packet.safeZone;
            }

            // 9. ships
            for (const auto &ship : packet.ships)
            {
                if (config_.logPackets && !ship.changes.empty())
                {
                    std::string shipName = "Unknown";
                    for (const auto &c : ship.changes)
                    {
                        if (c.id == ParamId::USERNAME)
                        {
                            if (std::holds_alternative<std::string>(c.data))
                                shipName = std::get<std::string>(c.data);
                        }
                    }
                    appendEngineLog(std::format("[GameState] Ship update: id={} name={} changes={}",
                                                ship.id, shipName, ship.changes.size()));
                }

                updateShip(ship.id, ship.changes, ship.destroyed, ship.relation, ship.clanRelation);
            }
            // If we got game state while loading, transition to InGame
            if (state_ == EngineState::Loading)
            {
                setState(EngineState::InGame);
            }

            if (config_.logPackets && (!packet.ships.empty() || !packet.collectables.empty()))
            {
                std::cout << std::format("[GameState] ships={} collectables={} events={}\n",
                                         packet.ships.size(), packet.collectables.size(), packet.events.size());
                appendEngineLog(std::format("[GameState] ships={} collectables={} events={}",
                                            packet.ships.size(), packet.collectables.size(), packet.events.size()));
            }
        }
        catch (const std::exception &e)
        {
            packetParseErrors_.fetch_add(1);
            std::cerr << std::format("[Engine] GameState parse error: {}\n", e.what());
            appendEngineLog(std::format("[Engine] GameState parse error: {}", e.what()));
        }
    }

    void GameEngine::handleUserInfo(KryoBuffer &buffer)
    {
        // UserInfoResponsePacket: params[]
        // Each param: data (Object), id (int), type (int) - alphabetical

        try
        {
            UserInfoResponsePacket packet;
            packet.deserialize(buffer);

            std::optional<int32_t> userInfoFraction;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);

                for (const auto &param : packet.params)
                {
                    // Extract numeric value from variant data
                    int64_t value = 0;
                    if (std::holds_alternative<int32_t>(param.data))
                    {
                        value = std::get<int32_t>(param.data);
                    }
                    else if (std::holds_alternative<int64_t>(param.data))
                    {
                        value = std::get<int64_t>(param.data);
                    }
                    else if (std::holds_alternative<float>(param.data))
                    {
                        value = static_cast<int64_t>(std::get<float>(param.data));
                    }
                    else if (std::holds_alternative<bool>(param.data))
                    {
                        value = std::get<bool>(param.data) ? 1 : 0;
                    }

                    switch (param.id)
                    {
                    case ParamId::BTC:
                        hero_.btc = value;
                        if (config_.logPackets)
                            appendEngineLog(std::format("[UserInfo] BTC: {}", value));
                        break;
                    case ParamId::PLT:
                        hero_.plt = value;
                        if (config_.logPackets)
                            appendEngineLog(std::format("[UserInfo] PLT: {}", value));
                        break;
                    case ParamId::EXPERIENCE:
                        hero_.experience = value;
                        if (config_.logPackets)
                            appendEngineLog(std::format("[UserInfo] EXP: {}", value));
                        break;
                    case ParamId::HONOR:
                        hero_.honor = static_cast<int32_t>(value);
                        if (config_.logPackets)
                            appendEngineLog(std::format("[UserInfo] Honor: {}", value));
                        break;
                    case ParamId::LEVEL:
                        hero_.level = static_cast<int32_t>(value);
                        if (config_.logPackets)
                            appendEngineLog(std::format("[UserInfo] Level: {}", value));
                        break;
                    case ParamId::BOOTY_KEYS:
                        hero_.bootyKeys = static_cast<int32_t>(value);
                        if (config_.logPackets)
                            appendEngineLog(std::format("[UserInfo] Keys: {}", value));
                        break;

                    case ParamId::LASERS:
                        hero_.lasers.set(param.type, static_cast<int32_t>(value));
                        break;

                    case ParamId::ROCKETS:
                        hero_.rockets.set(param.type, static_cast<int32_t>(value));
                        break;

                    case ParamId::ENERGY:
                        hero_.energy.set(param.type, static_cast<int32_t>(value));
                        break;

                    case ParamId::CARGO:
                        hero_.cargo = static_cast<int32_t>(value);
                        break;
                    case ParamId::MAX_CARGO:
                        hero_.maxCargo = static_cast<int32_t>(value);
                        break;

                    case ParamId::MAP_ID:
                        mapInfo_.id = static_cast<int32_t>(value);
                        if (config_.logPackets)
                            appendEngineLog(std::format("[UserInfo] Map changed to ID: {}", value));
                        break;
                    case ParamId::FACTION:
                        if (config_.logPackets)
                            appendEngineLog(std::format("[UserInfo] Faction: {}", value));
                        userInfoFraction = static_cast<int32_t>(value);
                        break;

                    case ParamId::HEARTBEAT:
                        break;

                    default:
                        // Log unknown params if needed
                        break;
                    }
                }
            }

            if (userInfoFraction.has_value())
            {
                updateHeroFraction(*userInfoFraction, "user-info");
            }
        }
        catch (const std::exception &e)
        {
            if (config_.logPackets)
            {
                std::cerr << std::format("[Engine] UserInfo parse error: {}\n", e.what());
                appendEngineLog(std::format("[Engine] UserInfo parse error: {}", e.what()));
            }
        }
    }
    void GameEngine::handleGameEvent(int32_t eventId, const std::string &data)
    {
        switch (eventId)
        {
        case GameEventId::SHIP_DESTROYED: // Player died
            updateDeathState(true, "game-event", true);
            break;

        case GameEventId::SHIP_REVIVED: // Player revived
            updateDeathState(false, "game-event");
            break;

        case GameEventId::CARGO_FULL:
            if (config_.logPackets)
            {
                std::cout << "[Engine] Cargo full!\n";
                appendEngineLog("[Engine] Cargo full!");
            }
            break;

        case GameEventId::LEVEL_UP:
            if (config_.logPackets)
            {
                std::cout << "[Engine] Level up!\n";
                appendEngineLog("[Engine] Level up!");
            }
            break;

        case GameEventId::CONVOY_ACTIVE:
        {
            int32_t active = 0;
            if (parseIntJson(data, active) && active == 0)
            {
                entities_.clearConvoys();
            }
        }
            if (config_.logPackets)
            {
                appendEngineLog(std::format("[Convoy] Active flag payload={}", data));
            }
            break;

        case GameEventId::CONVOY_TARGET:
        {
            std::array<int32_t, 4> convoyData{};
            if (parseIntArrayJson(data, convoyData))
            {
                ConvoyInfo convoy;
                convoy.sourceShipId = convoyData[0];
                convoy.npcShipId = convoyData[1];
                convoy.state = convoyData[2];
                convoy.phase = convoyData[3];
                convoy.lastUpdateTime = Clock::now();
                entities_.updateConvoy(convoy.npcShipId, convoy);
                if (config_.logPackets)
                {
                    appendEngineLog(std::format(
                        "[Convoy] source={} npc={} state={} phase={}",
                        convoy.sourceShipId, convoy.npcShipId, convoy.state, convoy.phase));
                }
            }
            break;
        }

        default:
            if (config_.logPackets)
            {
                std::cout << std::format("[Engine] Game event: id={}, data={}\n", eventId, data);
                appendEngineLog(std::format("[Engine] Game event: id={}, data={}", eventId, data));
            }
            break;
        }
    }

    //------------------------------------------------------------------------------
    // Entity Management (private)
    //------------------------------------------------------------------------------

    void GameEngine::updateShip(int32_t shipId,
                                const std::vector<ChangedParameter> &changes,
                                bool destroyed,
                                int32_t relation,
                                int32_t clanRelation)
    {
        bool relationSignalsChanged = false;
        entities_.updateShip(shipId, [&](ShipInfo &ship)
                             {
        const bool previousNpc = ship.isNpc;
        const auto previousName = ship.name;
        const auto previousClanTag = ship.clanTag;
        const auto previousFraction = ship.fraction;
        const auto previousRelation = ship.relation;
        const auto previousClanRelation = ship.clanRelation;
        const auto previousEnemy = ship.isEnemy;
        const auto previousAlly = ship.isAlly;
        ship.isDestroyed = destroyed;
        ship.relation = relation;
        ship.clanRelation = clanRelation;
        // ShipInResponse.relation is not reliable enough for player diplomacy.
        // Use clanRelation as the explicit ally/enemy signal and fall back to fraction
        // comparison later when building the public snapshot.
        ship.isEnemy = clanRelation == 2;
        ship.isAlly = clanRelation == 1;
        
        for (const auto& change : changes) {
            switch (change.id) {
                case ParamId::USERNAME:
                    if (auto* s = std::get_if<std::string>(&change.data)) {
                        ship.name = *s;
                    }
                    break;
                    
                case ParamId::CLAN_TAG:
                    if (auto* s = std::get_if<std::string>(&change.data)) {
                        ship.clanTag = *s;
                    }
                    break;
                    
                case ParamId::FRACTION:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.fraction = *v;
                    }
                    break;
                    
                case ParamId::POSITION:
                    if (auto* arr = std::get_if<std::vector<int32_t>>(&change.data)) {
                        if (arr->size() >= 2) {
                            const float newX = static_cast<float>((*arr)[0]);
                            const float newY = static_cast<float>((*arr)[1]);
                            const auto posNow = std::chrono::steady_clock::now();
                            ship.updateObservedVelocity(newX, newY, posNow);
                            ship.x = newX;
                            ship.y = newY;
                            ship.lastCoordUpdateTime = posNow;
                            if (config_.logPackets) appendEngineLog(std::format("[Ship {}] Pos: {}, {}", shipId, ship.x, ship.y));
                            
                            // Check if reached target
                            if (std::abs(ship.x - ship.targetX) < 1.0f && 
                                std::abs(ship.y - ship.targetY) < 1.0f) {
                                ship.isMoving = false;
                            }
                        }
                    }
                    break;
                    
                case ParamId::TARGET_POS:
                    if (auto* arr = std::get_if<std::vector<int32_t>>(&change.data)) {
                        if (arr->size() >= 2) {
                            ship.targetX = static_cast<float>((*arr)[0]);
                            ship.targetY = static_cast<float>((*arr)[1]);
                            if (config_.logPackets) appendEngineLog(std::format("[Ship {}] Moving to: {}, {}", shipId, ship.targetX, ship.targetY));
                            if (ship.targetX != ship.x || ship.targetY != ship.y) {
                                ship.isMoving = true;
                            }
                        }
                    }
                    break;
                    
                case ParamId::POSITION_X:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        const float newPosX = static_cast<float>(*v);
                        const auto posXNow = std::chrono::steady_clock::now();
                        ship.updateObservedVelocity(newPosX, ship.y, posXNow);
                        ship.x = newPosX;
                        ship.lastCoordUpdateTime = posXNow;
                        if (config_.logPackets) appendEngineLog(std::format("[Ship {}] X: {}", shipId, ship.x));
                    }
                    break;

                case ParamId::POSITION_Y:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        const float newPosY = static_cast<float>(*v);
                        const auto posYNow = std::chrono::steady_clock::now();
                        ship.updateObservedVelocity(ship.x, newPosY, posYNow);
                        ship.y = newPosY;
                        ship.lastCoordUpdateTime = posYNow;
                        if (config_.logPackets) appendEngineLog(std::format("[Ship {}] Y: {}", shipId, ship.y));
                    }
                    break;
                    
                case ParamId::TARGET_X:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.targetX = static_cast<float>(*v);
                        if (config_.logPackets) appendEngineLog(std::format("[Ship {}] Target X: {}", shipId, ship.targetX));
                        if (ship.targetX != ship.x) ship.isMoving = true;
                    }
                    break;
                    
                case ParamId::TARGET_Y:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.targetY = static_cast<float>(*v);
                        if (config_.logPackets) appendEngineLog(std::format("[Ship {}] Target Y: {}", shipId, ship.targetY));
                        if (ship.targetY != ship.y) ship.isMoving = true;
                    }
                    break;
                    
                case ParamId::HEALTH:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.health = *v;
                        if (config_.logPackets) appendEngineLog(std::format("[Ship {}] HP: {}", shipId, ship.health));
                    }
                    break;
                    
                case ParamId::MAX_HEALTH:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.maxHealth = *v;
                    }
                    break;
                    
                case ParamId::SHIELD:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.shield = *v;
                        if (config_.logPackets) appendEngineLog(std::format("[Ship {}] Shield: {}", shipId, ship.shield));
                    }
                    break;
                    
                case ParamId::MAX_SHIELD:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.maxShield = *v;
                    }
                    break;
                    
                case ParamId::SPEED:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.speed = *v;
                    }
                    break;

                case ParamId::DRONES:
                    if (auto* arr = std::get_if<std::vector<int32_t>>(&change.data)) {
                        ship.droneCount = static_cast<int32_t>(arr->size());
                    }
                    break;
                    
                case ParamId::CARGO:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.cargo = *v;
                    }
                    break;
                    
                case ParamId::MAX_CARGO:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.maxCargo = *v;
                    }
                    break;
                    
                case ParamId::SELECTED_TARGET:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.selectedTarget = *v;
                        if (config_.logPackets) appendEngineLog(std::format("[Ship {}] Locked on: {}", shipId, ship.selectedTarget));
                    }
                    break;
                    
                case ParamId::IS_ATTACKING:
                    if (auto* v = std::get_if<bool>(&change.data)) {
                        ship.isAttacking = *v;
                        if (config_.logPackets) appendEngineLog(std::format("[Ship {}] Is attacking: {}", shipId, *v));
                    }
                    break;
                    
                case ParamId::IN_ATTACK_RANGE:
                    if (auto* v = std::get_if<bool>(&change.data)) {
                        ship.inAttackRange = *v;
                    }
                    break;
                    
                case ParamId::SHIP_TYPE:
                    if (auto* v = std::get_if<int32_t>(&change.data)) {
                        ship.shipType = *v;
                    }
                    break;
                    
                case ParamId::IS_CLOAKED:
                    if (auto* v = std::get_if<bool>(&change.data)) {
                        ship.isCloaked = *v;
                    }
                    break;
                    
                default:
                    // Unknown param - skip
                    break;
            }
        }

        ship.isNpc = shouldClassifyShipAsNpc(ship, playerId_);
        relationSignalsChanged =
            previousName != ship.name ||
            previousClanTag != ship.clanTag ||
            previousFraction != ship.fraction ||
            previousRelation != ship.relation ||
            previousClanRelation != ship.clanRelation ||
            previousEnemy != ship.isEnemy ||
            previousAlly != ship.isAlly ||
            previousNpc != ship.isNpc;
        if (config_.logPackets && previousNpc != ship.isNpc && !ship.name.empty()) {
            appendEngineLog(std::format(
                "[Classify] ship={} name={} npc={} relation={} clanRelation={} clanTag='{}'",
                shipId,
                ship.name,
                ship.isNpc,
                ship.relation,
                ship.clanRelation,
                ship.clanTag
            ));
        } });

        if (config_.logPackets && relationSignalsChanged && shipId != playerId_)
        {
            if (const auto ship = entities_.getShip(shipId);
                ship.has_value() && !ship->isNpc && !ship->name.empty())
            {
                int32_t playerFraction = 0;
                std::string playerClanTag;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    playerFraction = hero_.fraction;
                    playerClanTag = hero_.clanTag;
                }

                appendEngineLog(std::format(
                    "[Relation] ship={} name='{}' fraction={} playerFraction={} relation={} clanRelation={} enemy={} ally={} bucket={} clanTag='{}'",
                    shipId,
                    ship->name,
                    ship->fraction,
                    playerFraction,
                    ship->relation,
                    ship->clanRelation,
                    ship->isEnemy,
                    ship->isAlly,
                    classifyPlayerAffinity(*ship, playerFraction, playerClanTag),
                    ship->clanTag));
            }
        }

        // Update player state if this is our ship
        if (shipId == playerId_)
        {
            updatePlayerFromShip(shipId);
        }
    }

    void GameEngine::updateCollectable(const CollectableInPacket &coll)
    {
        if (coll.existOnMap)
        {
            entities_.updateBox(coll.id, [&](BoxInfo &box)
                                {
            box.type = coll.type;
            box.subtype = coll.subtype;
            box.x = static_cast<float>(coll.x);
            box.y = static_cast<float>(coll.y);
            box.existsOnMap = true; });
        }
        else
        {
            // Remove from map
            entities_.removeBox(coll.id);
        }
    }

    void GameEngine::updatePlayerFromShip(int32_t shipId)
    {
        auto ship = entities_.getShip(shipId);
        if (!ship)
            return;
        const bool deadFromShip = ship->isDestroyed || (ship->maxHealth > 0 && ship->health == 0);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            hero_.x = ship->x;
            hero_.y = ship->y;
            const auto now = Clock::now();
            const bool keepLocalTarget = hasLocalMoveTarget_ && now < localMoveTargetExpiry_;
            if (keepLocalTarget)
            {
                hero_.targetX = localMoveTargetX_;
                hero_.targetY = localMoveTargetY_;
                hero_.isMoving = true;
            }
            else
            {
                hasLocalMoveTarget_ = false;
                hero_.targetX = ship->targetX;
                hero_.targetY = ship->targetY;
                hero_.isMoving = ship->isMoving;
            }
            hero_.health = ship->health;
            hero_.maxHealth = ship->maxHealth;
            hero_.shield = ship->shield;
            hero_.maxShield = ship->maxShield;
            hero_.speed = ship->speed;
            hero_.name = ship->name;
            hero_.clanTag = ship->clanTag;
            hero_.selectedTarget = ship->selectedTarget;
            hero_.isAttacking = ship->isAttacking;
            hero_.inAttackRange = ship->inAttackRange;

            if (hasLocalMoveTarget_)
            {
                const float dx = hero_.x - localMoveTargetX_;
                const float dy = hero_.y - localMoveTargetY_;
                if (std::sqrt(dx * dx + dy * dy) < 8.0f || now >= localMoveTargetExpiry_)
                {
                    hasLocalMoveTarget_ = false;
                }
            }

            hero_.lastUpdateTime = std::chrono::steady_clock::now();
        }

        if (ship->fraction > 0)
        {
            updateHeroFraction(ship->fraction, "player-ship");
        }

        updateDeathState(deadFromShip, "game-state", false);
    }

    void GameEngine::updateHeroFraction(int32_t fraction, const char *source)
    {
        if (fraction <= 0)
        {
            return;
        }

        int32_t previousFraction = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            previousFraction = hero_.fraction;
            if (previousFraction == fraction)
            {
                return;
            }
            hero_.fraction = fraction;
        }

        if (config_.logPackets)
        {
            appendEngineLog(std::format(
                "[Hero] Fraction {} -> {} via {}",
                previousFraction,
                fraction,
                source));
        }
    }

    void GameEngine::pruneStaleEntities()
    {
        entities_.pruneStale(config_.staleEntityTimeoutMs);
    }

    //------------------------------------------------------------------------------
    // Sending (private)
    //------------------------------------------------------------------------------

    void GameEngine::sendPacket(const std::vector<uint8_t> &data)
    {
        if (!kryo_)
            return;

        if (config_.logPackets && !data.empty())
        {
            try
            {
                KryoBuffer inspect{std::vector<uint8_t>(data)};
                const int32_t wireClassId = inspect.readVarInt(true);
                const bool frameworkPacket = isFrameworkWireClass(wireClassId);
                const int32_t packetId = frameworkPacket ? -wireClassId : decodeKryoPacketId(wireClassId);
                const auto packetName = frameworkPacket
                                            ? frameworkPacketName(wireClassId)
                                            : PacketRegistry::getPacketName(static_cast<PacketId>(packetId));

                appendEngineLog(std::format("[OutPacket] wire={} id={} ({}) size: {}",
                                            wireClassId, packetId, packetName, data.size()));

                std::string hexDump = "[OutHex] ";
                const size_t dumpLen = std::min(data.size(), size_t{64});
                for (size_t i = 0; i < dumpLen; ++i)
                {
                    hexDump += std::format("{:02X} ", data[i]);
                }
                if (data.size() > 64)
                {
                    hexDump += "...";
                }
                appendEngineLog(hexDump);
            }
            catch (...)
            {
                appendEngineLog(std::format("[OutPacket] size: {}", data.size()));
            }
        }

        // sendRaw expects framed data; send() takes KryoBuffer and frames it
        // We need to frame our raw buffer data
        KryoBuffer buf{std::vector<uint8_t>(data)};
        kryo_->send(buf);
        lastNetworkWrite_ = Clock::now();
    }

    void GameEngine::sendUserAction(int32_t actionId, const std::string &data)
    {
        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::UserActionsPacket), true);

        UserActionsPacket packet(UserAction(actionId, data));
        packet.serialize(buffer);

        std::string actionName = "action_" + std::to_string(actionId);
        queueAction(buffer.data(), 0, actionName);
    }

    void GameEngine::requestShopItems()
    {
        sendApiRequest(WuApiUri::SHOP_ITEMS, "{}");
        appendEngineLog("[Shop] Requesting item catalog...");
    }

    void GameEngine::buyShopItem(const std::string &itemId, int32_t quantity, int32_t price)
    {
        nlohmann::json body;
        body["itemId"] = itemId;
        body["quantity"] = quantity;
        body["price"] = price;
        sendApiRequest(WuApiUri::SHOP_BUY, body.dump());
        appendEngineLog(std::format("[Shop] Buying itemId={} qty={} price={}", itemId, quantity, price));
    }

    void GameEngine::sendApiRequest(const std::string &uri, const std::string &jsonBody)
    {
        KryoBuffer buffer;
        const int32_t requestId = nextRequestId_++;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::ApiRequestPacket), true);

        ApiRequestPacket packet;
        packet.requestId = requestId;
        packet.uri = uri;
        packet.requestDataJson = jsonBody;
        packet.serialize(buffer);

        if (config_.logPackets)
        {
            appendEngineLog(std::format(
                "[Engine] Sending ApiRequestPacket requestId={} uri='{}' bodySize={}",
                requestId, uri, jsonBody.size()));

            // Hex dump first 64 bytes for debugging
            std::string hexDump = "[OutHex] ";
            const auto &data = buffer.data();
            size_t dumpLen = std::min(data.size(), size_t{64});
            for (size_t i = 0; i < dumpLen; ++i)
            {
                hexDump += std::format("{:02X} ", data[i]);
            }
            if (data.size() > 64)
                hexDump += "...";
            appendEngineLog(hexDump);
        }

        sendPacket(buffer.data());
    }

    void GameEngine::sendAuthFractionRequest()
    {
        if (authFractionRequested_.exchange(true))
        {
            return;
        }

        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::AuthFractionRequestPacket), true);
        AuthFractionRequestPacket packet;
        packet.serialize(buffer);

        if (config_.logPackets)
        {
            appendEngineLog("[Engine] Sending AuthFractionRequestPacket");
        }

        sendPacket(buffer.data());
    }

    void GameEngine::sendFrameworkKeepAlivePacket()
    {
        KryoBuffer buffer;
        buffer.writeVarInt(FrameworkWireClass::KeepAlive, true);
        sendPacket(buffer.data());
    }

    void GameEngine::sendClientResumePacket()
    {
        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::ClientOnResumePacket), true);
        ClientOnResumePacket packet;
        packet.serialize(buffer);
        sendPacket(buffer.data());
    }

    void GameEngine::sendClientInfoPacket(bool synchronize)
    {
        ClientInfoNetPacket packet;
        packet.synchronize = synchronize;
        packet.json = buildClientInfoJson();

        KryoBuffer buffer;
        buffer.writeVarInt(encodeKryoPacketId(PacketId::ClientInfoNetPacket), true);
        packet.serialize(buffer);
        sendPacket(buffer.data());
    }

    std::string GameEngine::buildClientInfoJson() const
    {
        return buildClientInfoPayload(config_, clientVersion_).dump();
    }

} // namespace dynamo
