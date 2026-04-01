#pragma once

#include "backend_snapshots.hpp"
#include "host/ipc/ipc_pipe_server.hpp"

#include "bot/core/bot_controller.hpp"
#include "config/config_service.hpp"
#include "game/game_engine.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace dynamo::host {

struct BackendHostOptions {
    std::string pipeName{"DYNAMO_IPC"};
    std::string username;
    std::string password;
    std::string serverId{"eu1"};
    std::string language{"en"};
    bool logPackets{false};
    bool autoConnect{false};
};

class BackendHost {
public:
    explicit BackendHost(BackendHostOptions options);
    ~BackendHost();

    BackendHost(const BackendHost&) = delete;
    BackendHost& operator=(const BackendHost&) = delete;

    int run();
    void requestShutdown();

private:
    void setupEngineCallbacks();
    void initializeEngineFromOptions();
    bool connectGame(const BackendConnectRequest& request);
    void disconnectGame(bool stopBot);
    void refreshResourcePanels();
    void processCommands();
    void handleCommand(const IpcPipeServer::Command& command);
    void broadcastStatus();
    void broadcastProfiles();
    void broadcastActiveProfileDocument();
    void sendCommandResult(
        const IpcPipeServer::Command& command,
        bool success,
        std::string message
    );
    void log(const std::string& message);
    void resetSessionTracking();
    void updateSessionTracking(
        const HeroSnapshot& hero,
        EngineState engineState,
        BackendStatusSnapshot& snapshot
    );
    [[nodiscard]] static long long steadyNowMs();

    [[nodiscard]] BackendStatusSnapshot buildStatusSnapshot();
    [[nodiscard]] ProfileListSnapshot buildProfilesSnapshot() const;

    static std::string toModeString(BotMode mode);
    static std::string toBotStateString(BotState state);
    static std::string toCombatStateString(CombatState state);
    static std::string toTravelStateString(TravelState state);
    static std::string selectedTargetLabel(const HeroSnapshot& hero, const EntitiesSnapshot& entities);

    BackendHostOptions options_;
    std::atomic<bool> shutdownRequested_{false};
    bool engineInitialized_{false};

    std::shared_ptr<GameEngine> engine_;
    std::shared_ptr<ConfigService> configService_;
    std::unique_ptr<BotController> bot_;
    IpcPipeServer ipc_;

    struct SessionTrackingState {
        bool lastBotRunning{false};
        bool lastSessionActive{false};
        bool sessionInitialized{false};
        long long activeRunStartedAtMs{0};
        long long lastRuntimeMs{0};
        InventoryStatsSnapshot baseline;
        InventoryStatsSnapshot session;
    } sessionTracking_;

    std::chrono::steady_clock::time_point lastStatusBroadcast_{};
    std::chrono::steady_clock::time_point lastResourceRefresh_{};
};

} // namespace dynamo::host
