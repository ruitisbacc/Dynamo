#include "backend_host.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

constexpr auto kHostLoopInterval = std::chrono::milliseconds(16);
constexpr auto kStatusBroadcastInterval = std::chrono::milliseconds(16);
constexpr auto kResourceRefreshInterval = std::chrono::seconds(2);

bool isTradeMapName(const std::string& mapName) {
    return mapName.ends_with("-1") || mapName.ends_with("-7");
}

std::string laserName(int type) {
    switch (type) {
        case 1: return "RLX-1";
        case 2: return "GLX-2";
        case 3: return "BLX-3";
        case 4: return "WLX-4";
        case 5: return "GLX-2-AS";
        case 6: return "MRS-6X";
        default: return "-";
    }
}

std::string rocketName(int type) {
    switch (type) {
        case 1: return "KEP-410";
        case 2: return "NC-30";
        case 3: return "TNC-130";
        default: return "-";
    }
}

std::string targetCategory(const dynamo::ShipInfo& ship) {
    if (ship.isNpc) {
        return "NPC";
    }
    if (ship.isEnemy) {
        return "Enemy";
    }
    if (ship.isAlly) {
        return "Ally";
    }
    return "Ship";
}

dynamo::host::InventoryStatsSnapshot inventoryStatsFromHero(const dynamo::HeroSnapshot& hero) {
    dynamo::host::InventoryStatsSnapshot stats;
    stats.plt = hero.plt;
    stats.btc = hero.btc;
    stats.experience = hero.experience;
    stats.honor = hero.honor;
    stats.laserRlx1 = hero.lasers.rlx1;
    stats.laserGlx2 = hero.lasers.glx2;
    stats.laserBlx3 = hero.lasers.blx3;
    stats.laserWlx4 = hero.lasers.wlx4;
    stats.laserGlx2As = hero.lasers.glx2as;
    stats.laserMrs6X = hero.lasers.mrs6x;
    stats.rocketKep410 = hero.rockets.kep410;
    stats.rocketNc30 = hero.rockets.nc30;
    stats.rocketTnc130 = hero.rockets.tnc130;
    stats.energyEe = hero.energy.ee;
    stats.energyEn = hero.energy.en;
    stats.energyEg = hero.energy.eg;
    stats.energyEm = hero.energy.em;
    return stats;
}

dynamo::host::ResourceInventorySnapshot resourceInventoryFromState(
    const dynamo::ResourceStateSnapshot& resourceState
) {
    dynamo::host::ResourceInventorySnapshot snapshot;

    auto amountOf = [&](dynamo::ResourceType type) -> long long {
        const auto* resource = resourceState.findResource(static_cast<int32_t>(type));
        return resource ? resource->amount : 0;
    };

    snapshot.cerium = amountOf(dynamo::ResourceType::Cerium);
    snapshot.mercury = amountOf(dynamo::ResourceType::Mercury);
    snapshot.erbium = amountOf(dynamo::ResourceType::Erbium);
    snapshot.piritid = amountOf(dynamo::ResourceType::Piritid);
    snapshot.darkonit = amountOf(dynamo::ResourceType::Darkonit);
    snapshot.uranit = amountOf(dynamo::ResourceType::Uranit);
    snapshot.azurit = amountOf(dynamo::ResourceType::Azurit);
    snapshot.dungid = amountOf(dynamo::ResourceType::Dungid);
    snapshot.xureon = amountOf(dynamo::ResourceType::Xureon);
    return snapshot;
}

dynamo::host::InventoryStatsSnapshot subtractInventoryStats(
    const dynamo::host::InventoryStatsSnapshot& current,
    const dynamo::host::InventoryStatsSnapshot& baseline
) {
    dynamo::host::InventoryStatsSnapshot delta;
    delta.plt = current.plt - baseline.plt;
    delta.btc = current.btc - baseline.btc;
    delta.experience = current.experience - baseline.experience;
    delta.honor = current.honor - baseline.honor;
    delta.laserRlx1 = current.laserRlx1 - baseline.laserRlx1;
    delta.laserGlx2 = current.laserGlx2 - baseline.laserGlx2;
    delta.laserBlx3 = current.laserBlx3 - baseline.laserBlx3;
    delta.laserWlx4 = current.laserWlx4 - baseline.laserWlx4;
    delta.laserGlx2As = current.laserGlx2As - baseline.laserGlx2As;
    delta.laserMrs6X = current.laserMrs6X - baseline.laserMrs6X;
    delta.rocketKep410 = current.rocketKep410 - baseline.rocketKep410;
    delta.rocketNc30 = current.rocketNc30 - baseline.rocketNc30;
    delta.rocketTnc130 = current.rocketTnc130 - baseline.rocketTnc130;
    delta.energyEe = current.energyEe - baseline.energyEe;
    delta.energyEn = current.energyEn - baseline.energyEn;
    delta.energyEg = current.energyEg - baseline.energyEg;
    delta.energyEm = current.energyEm - baseline.energyEm;
    return delta;
}

} // namespace

namespace dynamo::host {

BackendHost::BackendHost(BackendHostOptions options)
    : options_(std::move(options))
    , engine_(std::make_shared<GameEngine>())
    , configService_(std::make_shared<ConfigService>())
    , bot_(std::make_unique<BotController>(engine_, configService_))
    , ipc_(options_.pipeName) {
    resetSessionTracking();
    setupEngineCallbacks();
    initializeEngineFromOptions();

    ipc_.onClientConnected([this]() {
        log("GUI client connected.");
        refreshResourcePanels();
        broadcastProfiles();
        broadcastStatus();
        broadcastActiveProfileDocument();
    });

    ipc_.onClientDisconnected([this]() {
        log("GUI client disconnected.");
    });
}

BackendHost::~BackendHost() {
    requestShutdown();
    if (bot_) {
        bot_->stop();
    }
    if (engine_) {
        engine_->disconnect();
        engine_->shutdown();
    }
    ipc_.stop();
}

int BackendHost::run() {
    ipc_.start();
    log("Backend host started.");
    log("Named pipe: " + ipc_.pipeName());

    using clock = std::chrono::steady_clock;
    lastStatusBroadcast_ = clock::now();
    lastResourceRefresh_ = clock::now() - kResourceRefreshInterval;

    while (!shutdownRequested_.load()) {
        processCommands();

        if (engineInitialized_) {
            engine_->update();
        }

        const auto now = clock::now();
        if (ipc_.hasClient() &&
            engineInitialized_ &&
            engine_->state() == EngineState::InGame &&
            now - lastResourceRefresh_ >= kResourceRefreshInterval) {
            refreshResourcePanels();
        }

        if (ipc_.hasClient() &&
            now - lastStatusBroadcast_ >= kStatusBroadcastInterval) {
            broadcastStatus();
            lastStatusBroadcast_ = now;
        }

        // Match the legacy GUI update cadence during login and early in-game sync.
        std::this_thread::sleep_for(kHostLoopInterval);
    }

    log("Backend host shutting down.");
    return 0;
}

void BackendHost::requestShutdown() {
    shutdownRequested_ = true;
}

void BackendHost::setupEngineCallbacks() {
    engine_->onStateChange([this](EngineState newState, EngineState oldState) {
        log(
            "Engine state: " +
            std::string(engineStateToString(oldState)) +
            " -> " +
            std::string(engineStateToString(newState))
        );
        broadcastStatus();
    });

    engine_->onError([this](const std::string& error) {
        log("Engine error: " + error);
        broadcastStatus();
    });

    engine_->onMapLoaded([this](const MapInfo& mapInfo) {
        log("Map loaded: " + mapInfo.name);
        broadcastStatus();
        // Auto-fetch shop catalog on map load for autobuy
        engine_->requestShopItems();
        refreshResourcePanels();
    });

    engine_->onDeath([this]() {
        log("Hero death detected.");
        if (bot_) {
            bot_->handleImmediateDeathEvent();
        }
        broadcastStatus();
    });

    engine_->onRevive([this]() {
        log("Hero revived.");
        if (bot_) {
            bot_->handleReviveEvent();
        }
        refreshResourcePanels();
        broadcastStatus();
    });
}

void BackendHost::initializeEngineFromOptions() {
    if (options_.username.empty() || options_.password.empty()) {
        log("No login credentials provided. Host is starting in disconnected mode.");
        return;
    }

    if (!options_.autoConnect) {
        EngineConfig config;
        config.username = options_.username;
        config.password = options_.password;
        config.serverId = options_.serverId.empty() ? "eu1" : options_.serverId;
        config.language = options_.language.empty() ? "en" : options_.language;
        config.logPackets = options_.logPackets;

        engine_->shutdown();
        engineInitialized_ = engine_->initialize(config);
        if (engineInitialized_) {
            log("Engine initialized for server " + config.serverId + ".");
            log("Startup credentials loaded. Waiting for explicit connect request.");
        } else {
            log("Engine initialization failed.");
        }
        broadcastStatus();
        return;
    }

    BackendConnectRequest request;
    request.username = options_.username;
    request.password = options_.password;
    request.serverId = options_.serverId;
    request.language = options_.language;

    connectGame(request);
}

bool BackendHost::connectGame(const BackendConnectRequest& request) {
    if (request.username.empty() || request.password.empty()) {
        log("Connect request rejected: username and password are required.");
        return false;
    }

    if (bot_->getState() != BotState::Stopped) {
        bot_->stop();
    }

    engine_->shutdown();
    engineInitialized_ = false;

    EngineConfig config;
    config.username = request.username;
    config.password = request.password;
    config.serverId = request.serverId.empty() ? "eu1" : request.serverId;
    config.language = request.language.empty() ? "en" : request.language;
    config.logPackets = options_.logPackets;

    engineInitialized_ = engine_->initialize(config);
    if (!engineInitialized_) {
        log("Engine initialization failed.");
        broadcastStatus();
        return false;
    }

    log("Engine initialized for server " + config.serverId + ".");

    if (!engine_->connect()) {
        log("Connect request failed: " + engine_->lastError());
        broadcastStatus();
        return false;
    }

    log("Connect requested for user " + config.username + ".");
    resetSessionTracking();
    broadcastStatus();
    return true;
}

void BackendHost::disconnectGame(bool stopBot) {
    if (stopBot && bot_->getState() != BotState::Stopped) {
        bot_->stop();
    }

    if (engine_->state() != EngineState::NotConnected &&
        engine_->state() != EngineState::Disconnected) {
        engine_->disconnect();
    }

    resetSessionTracking();
    broadcastStatus();
}

void BackendHost::refreshResourcePanels() {
    if (!engineInitialized_ || engine_->state() != EngineState::InGame) {
        return;
    }

    // Resource data is exposed by explicitly requesting the same packet flow
    // the client uses when opening the resources/equipment panels.
    engine_->requestResourcesInfo();

    if (isTradeMapName(engine_->currentMap())) {
        engine_->requestResourcesTradeInfo();
    }

    lastResourceRefresh_ = std::chrono::steady_clock::now();
}

void BackendHost::processCommands() {
    while (const auto command = ipc_.popCommand()) {
        handleCommand(*command);
    }
}

void BackendHost::handleCommand(const IpcPipeServer::Command& command) {
    switch (command.type) {
        case MessageType::GetStatus:
            broadcastStatus();
            sendCommandResult(command, true, "Status snapshot sent.");
            break;

        case MessageType::GetProfiles:
            broadcastProfiles();
            sendCommandResult(command, true, "Profiles snapshot sent.");
            break;

        case MessageType::StartBot:
            if (bot_->isPaused()) {
                if (!engine_->isConnected()) {
                    log("Cannot resume bot while engine is not in game.");
                    sendCommandResult(command, false, "Engine is not in game.");
                    break;
                }
                bot_->resume();
                log("Bot resume requested.");
                broadcastStatus();
                sendCommandResult(command, true, "Bot resume accepted.");
                break;
            }
            if (bot_->getState() != BotState::Stopped) {
                log("Bot is already running.");
                sendCommandResult(command, false, "Bot is already running.");
                break;
            }
            if (!engine_->isConnected()) {
                log("Cannot start bot while engine is not in game.");
                sendCommandResult(command, false, "Engine is not in game.");
                break;
            }
            resetSessionTracking();
            bot_->start();
            log("Bot start requested.");
            broadcastStatus();
            sendCommandResult(command, true, "Bot start accepted.");
            break;

        case MessageType::PauseBot:
            if (bot_->isRunning()) {
                bot_->pause();
                log("Bot pause requested.");
                sendCommandResult(command, true, "Bot pause accepted.");
            } else if (bot_->isPaused()) {
                log("Bot is already paused.");
                sendCommandResult(command, false, "Bot is already paused.");
            } else {
                log("Bot is not running.");
                sendCommandResult(command, false, "Bot is not running.");
            }
            broadcastStatus();
            break;

        case MessageType::StopBot:
            if (bot_->getState() != BotState::Stopped) {
                bot_->stop();
                log("Bot stop requested.");
                sendCommandResult(command, true, "Bot stop accepted.");
            } else {
                log("Bot is already stopped.");
                sendCommandResult(command, false, "Bot is already stopped.");
            }
            broadcastStatus();
            break;

        case MessageType::LoadProfile: {
            const std::string profileId(command.payload.begin(), command.payload.end());
            std::string error;
            if (profileId.empty()) {
                log("Load profile ignored: empty profile id.");
                sendCommandResult(command, false, "Profile id is required.");
                break;
            }
            if (bot_->selectProfile(profileId, &error)) {
                log("Active profile changed to " + profileId + ".");
                broadcastProfiles();
                broadcastStatus();
                broadcastActiveProfileDocument();
                sendCommandResult(command, true, "Profile loaded.");
            } else {
                log("Failed to load profile '" + profileId + "': " + error);
                sendCommandResult(command, false, error.empty() ? "Failed to load profile." : error);
            }
            break;
        }

        case MessageType::SaveProfile: {
            const auto active = configService_->activeProfile();
            if (!active.has_value()) {
                log("Failed to save profile: no active profile.");
                sendCommandResult(command, false, "No active profile.");
                break;
            }

            std::string error;
            if (configService_->saveProfile(*active, true, &error)) {
                log("Saved profile '" + active->id + "'.");
                broadcastProfiles();
                broadcastStatus();
                broadcastActiveProfileDocument();
                sendCommandResult(command, true, "Profile saved.");
            } else {
                log("Failed to save profile '" + active->id + "': " + error);
                sendCommandResult(command, false, error.empty() ? "Failed to save profile." : error);
            }
            break;
        }

        case MessageType::GetActiveProfile:
            broadcastActiveProfileDocument();
            sendCommandResult(command, true, "Active profile document sent.");
            break;

        case MessageType::SaveProfileDocument: {
            try {
                const auto json = nlohmann::json::parse(command.payload);
                auto profile = json.get<BotProfile>();

                std::string error;
                if (configService_->saveProfile(std::move(profile), true, &error)) {
                    const auto active = configService_->activeProfile();
                    log("Saved profile document '" +
                        (active.has_value() ? active->id : std::string("unknown")) + "'.");
                    broadcastProfiles();
                    broadcastStatus();
                    broadcastActiveProfileDocument();
                    sendCommandResult(command, true, "Profile document saved.");
                } else {
                    log("Failed to save profile document: " + error);
                    sendCommandResult(
                        command,
                        false,
                        error.empty() ? "Failed to save profile document." : error
                    );
                }
            } catch (const std::exception& ex) {
                log(std::string("Invalid profile document payload: ") + ex.what());
                sendCommandResult(command, false, ex.what());
            }
            break;
        }

        case MessageType::ConnectGame: {
            try {
                const auto json = nlohmann::json::parse(command.payload);
                const auto request = json.get<BackendConnectRequest>();
                log("Connect request received for server " +
                    (request.serverId.empty() ? std::string("eu1") : request.serverId) + ".");
                if (connectGame(request)) {
                    sendCommandResult(command, true, "Connect request accepted.");
                } else {
                    const auto error = engine_->lastError();
                    sendCommandResult(
                        command,
                        false,
                        error.empty() ? "Connect request failed." : error
                    );
                }
            } catch (const std::exception& ex) {
                log(std::string("Invalid connect request payload: ") + ex.what());
                broadcastStatus();
                sendCommandResult(command, false, ex.what());
            }
            break;
        }

        case MessageType::DisconnectGame:
            disconnectGame(true);
            log("Disconnect requested.");
            sendCommandResult(command, true, "Disconnect accepted.");
            break;

        case MessageType::MoveTo: {
            try {
                const auto json = nlohmann::json::parse(command.payload);
                const float x = json.value("x", 0.0f);
                const float y = json.value("y", 0.0f);
                if (!engine_->isConnected()) {
                    sendCommandResult(command, false, "Engine is not in game.");
                    break;
                }
                engine_->moveTo(x, y);
                broadcastStatus();
                sendCommandResult(command, true, "MoveTo accepted.");
            } catch (const std::exception& ex) {
                log(std::string("Invalid MoveTo payload: ") + ex.what());
                sendCommandResult(command, false, ex.what());
            }
            break;
        }

        case MessageType::RequestShutdown:
            log("Shutdown requested by GUI.");
            sendCommandResult(command, true, "Shutdown accepted.");
            requestShutdown();
            break;

        default:
            log("Unknown IPC command received.");
            sendCommandResult(command, false, "Unknown IPC command.");
            break;
    }
}

void BackendHost::broadcastStatus() {
    const auto snapshot = buildStatusSnapshot();
    const auto payload = nlohmann::json(snapshot).dump();
    ipc_.send(MessageType::StatusSnapshot, payload);
}

void BackendHost::broadcastProfiles() {
    const auto snapshot = buildProfilesSnapshot();
    const auto payload = nlohmann::json(snapshot).dump();
    ipc_.send(MessageType::ProfilesSnapshot, payload);
}

void BackendHost::broadcastActiveProfileDocument() {
    const auto profile = configService_->activeProfile();
    if (!profile.has_value()) {
        log("No active profile document available.");
        return;
    }

    const auto payload = nlohmann::json(*profile).dump();
    ipc_.send(MessageType::ProfileDocument, payload);
}

void BackendHost::sendCommandResult(
    const IpcPipeServer::Command& command,
    bool success,
    std::string message
) {
    if (command.requestId == 0) {
        return;
    }

    CommandResult result;
    result.requestId = command.requestId;
    result.commandType = static_cast<std::uint32_t>(command.type);
    result.success = success;
    result.message = std::move(message);

    const auto payload = nlohmann::json(result).dump();
    ipc_.send(MessageType::CommandResult, payload);
}

void BackendHost::log(const std::string& message) {
    std::cout << "[BackendHost] " << message << std::endl;
    ipc_.send(MessageType::LogLine, message);
}

long long BackendHost::steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void BackendHost::resetSessionTracking() {
    sessionTracking_ = SessionTrackingState{};
}

void BackendHost::updateSessionTracking(
    const HeroSnapshot& hero,
    EngineState engineState,
    BackendStatusSnapshot& snapshot
) {
    const bool botRunning = snapshot.botRunning;
    const bool botPaused = snapshot.botPaused;
    const bool sessionActive = botRunning || botPaused;
    const bool validSample = engineState == EngineState::InGame && hero.id != 0;
    const auto nowMs = steadyNowMs();
    const auto currentTotals = inventoryStatsFromHero(hero);

    snapshot.stats.total = currentTotals;

    if (sessionActive && !sessionTracking_.lastSessionActive) {
        sessionTracking_.sessionInitialized = false;
        sessionTracking_.baseline = InventoryStatsSnapshot{};
        sessionTracking_.session = InventoryStatsSnapshot{};
        sessionTracking_.lastRuntimeMs = 0;
        sessionTracking_.activeRunStartedAtMs = botRunning ? nowMs : 0;
    }

    if (botRunning && !sessionTracking_.lastBotRunning) {
        sessionTracking_.activeRunStartedAtMs = nowMs;
    } else if (!botRunning && sessionTracking_.lastBotRunning) {
        if (sessionTracking_.activeRunStartedAtMs > 0) {
            sessionTracking_.lastRuntimeMs +=
                std::max<long long>(0, nowMs - sessionTracking_.activeRunStartedAtMs);
            sessionTracking_.activeRunStartedAtMs = 0;
        }

        if (validSample && sessionTracking_.sessionInitialized) {
            sessionTracking_.session = subtractInventoryStats(
                currentTotals,
                sessionTracking_.baseline
            );
        }
    }

    if (botRunning) {
        if (validSample) {
            if (!sessionTracking_.sessionInitialized) {
                sessionTracking_.baseline = currentTotals;
                sessionTracking_.session = InventoryStatsSnapshot{};
                sessionTracking_.sessionInitialized = true;
            } else {
                sessionTracking_.session = subtractInventoryStats(
                    currentTotals,
                    sessionTracking_.baseline
                );
            }
        }

        snapshot.stats.runtimeMs = sessionTracking_.activeRunStartedAtMs > 0
            ? sessionTracking_.lastRuntimeMs +
                std::max<long long>(0, nowMs - sessionTracking_.activeRunStartedAtMs)
            : sessionTracking_.lastRuntimeMs;
    } else {
        snapshot.stats.runtimeMs = sessionTracking_.lastRuntimeMs;
    }

    snapshot.stats.session = sessionTracking_.session;
    sessionTracking_.lastBotRunning = botRunning;
    sessionTracking_.lastSessionActive = sessionActive;
}

BackendStatusSnapshot BackendHost::buildStatusSnapshot() {
    BackendStatusSnapshot snapshot;

    const auto engineState = engine_->state();
    const auto runtimeSnapshot = configService_->snapshot();
    const auto hero = engine_->hero();
    const auto entities = engine_->entities();
    const auto mapInfo = engine_->mapInfo();
    const auto resources = engine_->resourceState();

    snapshot.connectionState = engineState == EngineState::InGame
        ? "InGame"
        : std::string(engineStateToString(engineState));
    snapshot.engineState = engineStateToString(engineState);
    snapshot.engineError = engine_->lastError();
    snapshot.currentMap = engine_->currentMap().empty() ? "-" : engine_->currentMap();
    snapshot.botRunning = bot_->isRunning();
    snapshot.botPaused = bot_->isPaused();
    snapshot.currentTask = toBotStateString(bot_->getState());
    snapshot.currentTarget = selectedTargetLabel(hero, entities);
    snapshot.heroName = hero.name;
    snapshot.btc = hero.btc;
    snapshot.plt = hero.plt;
    snapshot.honor = hero.honor;
    snapshot.experience = hero.experience;
    snapshot.deathCount = bot_->getStats().deaths;
    snapshot.hpPercent = static_cast<int>(std::lround(hero.healthPercent()));
    snapshot.shieldPercent = static_cast<int>(std::lround(hero.shieldPercent()));
    snapshot.cargoPercent = static_cast<int>(std::lround(hero.cargoPercent()));
    snapshot.heroX = static_cast<int>(std::lround(hero.x));
    snapshot.heroY = static_cast<int>(std::lround(hero.y));
    snapshot.heroTargetX = static_cast<int>(std::lround(hero.targetX));
    snapshot.heroTargetY = static_cast<int>(std::lround(hero.targetY));
    snapshot.heroMoving = hero.isMoving;
    snapshot.mapWidth = mapInfo.width;
    snapshot.mapHeight = mapInfo.height;
    snapshot.activeConfig = hero.activeConfig;
    snapshot.currentLaser = laserName(hero.currentAmmoType);
    snapshot.currentRocket = rocketName(hero.currentRocketType);
    snapshot.currentResources = resourceInventoryFromState(resources);

    if (resources.hasResourcesInfo) {
        static constexpr std::pair<dynamo::ResourceModuleType, const char*> moduleNames[] = {
            {dynamo::ResourceModuleType::Lasers, "Lasers"},
            {dynamo::ResourceModuleType::Rockets, "Rockets"},
            {dynamo::ResourceModuleType::Shields, "Shields"},
            {dynamo::ResourceModuleType::Speed, "Speed"},
        };
        for (const auto& [moduleType, moduleName] : moduleNames) {
            const auto* enrichment = resources.findEnrichment(static_cast<int32_t>(moduleType));
            host::EnrichmentModuleSnapshot enrichSnap;
            enrichSnap.module = moduleName;
            if (enrichment && enrichment->amount > 0 && dynamo::isValidResourceType(enrichment->type)) {
                enrichSnap.material = dynamo::resourceTypeName(static_cast<dynamo::ResourceType>(enrichment->type));
                enrichSnap.amount = enrichment->amount;
            }
            snapshot.enrichments.push_back(std::move(enrichSnap));
        }
    }

    snapshot.npcCount = static_cast<int>(entities.npcs.size());
    snapshot.enemyCount = static_cast<int>(entities.enemies.size());
    snapshot.boxCount = static_cast<int>(entities.boxes.size());
    snapshot.portalCount = static_cast<int>(entities.portals.size());
    updateSessionTracking(hero, engineState, snapshot);

    // Map entities for GUI rendering
    for (const auto& npc : entities.npcs) {
        if (npc.id == hero.id) continue;
        snapshot.mapEntities.push_back({
            npc.id,
            static_cast<int>(std::lround(npc.x)),
            static_cast<int>(std::lround(npc.y)),
            0, npc.name
        });
    }
    for (const auto& enemy : entities.enemies) {
        if (enemy.id == hero.id) continue;
        snapshot.mapEntities.push_back({
            enemy.id,
            static_cast<int>(std::lround(enemy.x)),
            static_cast<int>(std::lround(enemy.y)),
            1, enemy.name
        });
    }
    for (const auto& ally : entities.allies) {
        if (ally.id == hero.id) continue;
        snapshot.mapEntities.push_back({
            ally.id,
            static_cast<int>(std::lround(ally.x)),
            static_cast<int>(std::lround(ally.y)),
            2, ally.name
        });
    }
    for (const auto& box : entities.boxes) {
        snapshot.mapEntities.push_back({
            box.id,
            static_cast<int>(std::lround(box.x)),
            static_cast<int>(std::lround(box.y)),
            3, ""
        });
    }
    for (const auto& portal : entities.portals) {
        snapshot.mapEntities.push_back({
            portal.id,
            static_cast<int>(std::lround(portal.x)),
            static_cast<int>(std::lround(portal.y)),
            4, portal.targetMapName
        });
    }
    for (const auto& station : entities.stations) {
        snapshot.mapEntities.push_back({
            station.id,
            static_cast<int>(std::lround(station.x)),
            static_cast<int>(std::lround(station.y)),
            5, station.name
        });
    }

    if (hero.selectedTarget != 0) {
        if (const auto target = entities.findShip(hero.selectedTarget); target.has_value()) {
            snapshot.hasTarget = true;
            snapshot.targetCategory = targetCategory(*target);
            snapshot.targetX = static_cast<int>(std::lround(target->x));
            snapshot.targetY = static_cast<int>(std::lround(target->y));
            snapshot.targetHpPercent = static_cast<int>(std::lround(target->healthPercent()));
            snapshot.targetShieldPercent = static_cast<int>(std::lround(target->shieldPercent()));
            snapshot.targetDistance = static_cast<int>(std::lround(target->distanceTo(hero.x, hero.y)));
        } else if (const auto box = entities.findBox(hero.selectedTarget); box.has_value()) {
            snapshot.hasTarget = true;
            snapshot.targetCategory = "Collectable";
            snapshot.targetX = static_cast<int>(std::lround(box->x));
            snapshot.targetY = static_cast<int>(std::lround(box->y));
            snapshot.targetDistance = static_cast<int>(std::lround(box->distanceTo(hero.x, hero.y)));
        }
    }

    if (runtimeSnapshot) {
        snapshot.activeProfile = runtimeSnapshot->sourceId.empty() ? "default" : runtimeSnapshot->sourceId;
        snapshot.workingMap = runtimeSnapshot->runtime.map.workingMap.empty()
            ? "-"
            : runtimeSnapshot->runtime.map.workingMap;
        snapshot.currentMode = toModeString(runtimeSnapshot->runtime.mode);
    }

    if (!snapshot.botRunning &&
        engineState != EngineState::NotConnected &&
        engineState != EngineState::InGame) {
        snapshot.currentTask = std::string(engineStateToString(engineState));
    }

    if (const auto safety = bot_->getSafetyTelemetry(); safety.has_value()) {
        snapshot.safetyActive = safety->state != SafetyState::Monitoring;
        snapshot.safetyReason = snapshot.safetyActive
            ? (safety->decision.empty() ? "Active" : safety->decision)
            : "-";
        snapshot.threatCount = std::max({safety->visibleEnemies, safety->closeEnemies, safety->attackers});
    }

    if (const auto combat = bot_->getCombatTelemetry(); combat.has_value()) {
        snapshot.combatState = toCombatStateString(combat->state);
        snapshot.combatDecision = combat->movementDecision.empty() ? "-" : combat->movementDecision;
        snapshot.combatMovement = "-";
        switch (combat->movementMode) {
            case CombatMovementMode::Default: snapshot.combatMovement = "Default"; break;
            case CombatMovementMode::Adaptive: snapshot.combatMovement = "Adaptive"; break;
            case CombatMovementMode::Direct: snapshot.combatMovement = "Direct"; break;
            case CombatMovementMode::Orbit: snapshot.combatMovement = "Orbit"; break;
            case CombatMovementMode::Kite: snapshot.combatMovement = "Kite"; break;
            case CombatMovementMode::Mixed: snapshot.combatMovement = "Mixed"; break;
        }
    }

    if (const auto travel = bot_->getTravelTelemetry(); travel.has_value()) {
        snapshot.travelState = toTravelStateString(travel->state);
        snapshot.travelDecision = travel->decision.empty() ? "-" : travel->decision;
        snapshot.travelDestination = travel->destinationMap.empty() ? "-" : travel->destinationMap;
    }

    if (const auto roaming = bot_->getRoamingTelemetry(); roaming.has_value()) {
        snapshot.roamingDecision = roaming->decision.empty() ? "Idle" : roaming->decision;
    }

    return snapshot;
}

ProfileListSnapshot BackendHost::buildProfilesSnapshot() const {
    ProfileListSnapshot snapshot;
    snapshot.activeProfile = configService_->activeProfileId();

    const auto profiles = configService_->profiles();
    snapshot.profiles.reserve(profiles.size());
    for (const auto& profile : profiles) {
        snapshot.profiles.push_back(profile.id);
    }
    std::sort(snapshot.profiles.begin(), snapshot.profiles.end());

    return snapshot;
}

std::string BackendHost::toModeString(BotMode mode) {
    switch (mode) {
        case BotMode::Kill: return "Kill";
        case BotMode::Collect: return "Collect";
        case BotMode::KillCollect: return "KillCollect";
        default: return "Idle";
    }
}

std::string BackendHost::toBotStateString(BotState state) {
    switch (state) {
        case BotState::Stopped: return "Stopped";
        case BotState::Starting: return "Starting";
        case BotState::Running: return "Running";
        case BotState::Paused: return "Paused";
        case BotState::Stopping: return "Stopping";
        default: return "Stopped";
    }
}

std::string BackendHost::toCombatStateString(CombatState state) {
    switch (state) {
        case CombatState::Searching: return "Searching";
        case CombatState::Approaching: return "Approaching";
        case CombatState::Attacking: return "Attacking";
        case CombatState::AwaitingKill: return "AwaitingKill";
        case CombatState::Cooldown: return "Cooldown";
        default: return "Searching";
    }
}

std::string BackendHost::toTravelStateString(TravelState state) {
    switch (state) {
        case TravelState::Idle: return "Idle";
        case TravelState::Planning: return "Planning";
        case TravelState::MovingToPortal: return "MovingToPortal";
        case TravelState::AtPortal: return "AtPortal";
        case TravelState::Jumping: return "Jumping";
        case TravelState::WaitingForMap: return "WaitingForMap";
        case TravelState::Arrived: return "Arrived";
        default: return "Idle";
    }
}

std::string BackendHost::selectedTargetLabel(const HeroSnapshot& hero, const EntitiesSnapshot& entities) {
    if (hero.selectedTarget == 0) {
        return "-";
    }

    if (const auto target = entities.findShip(hero.selectedTarget); target.has_value()) {
        return target->name.empty()
            ? ("#" + std::to_string(hero.selectedTarget))
            : target->name;
    }

    if (const auto box = entities.findBox(hero.selectedTarget); box.has_value()) {
        return "Collectable #" + std::to_string(box->id);
    }

    return "#" + std::to_string(hero.selectedTarget);
}

} // namespace dynamo::host
