#pragma once

/**
 * @file bot_controller.hpp
 * @brief Main bot controller - orchestrates all modules
 * 
 * Central point for bot operation. Manages game state,
 * module scheduling, and lifecycle.
 */

#include "scheduler.hpp"
#include "module.hpp"
#include "bot_config.hpp"
#include "bot/support/movement_controller.hpp"
#include "bot/modules/safety_module.hpp"
#include "bot/modules/combat_module.hpp"
#include "bot/modules/collect_module.hpp"
#include "bot/modules/roaming_module.hpp"
#include "bot/modules/travel_module.hpp"
#include "bot/resources/resource_planner.hpp"
#include "bot/support/map_graph.hpp"
#include "config/config_service.hpp"

#include "game/game_engine.hpp"
#include "game/hero.hpp"
#include "game/entities.hpp"

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <array>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace dynamo {

/**
 * @brief Bot running state
 */
enum class BotState {
    Stopped,        // Not running
    Starting,       // Initializing
    Running,        // Active
    Paused,         // Temporarily paused
    Stopping        // Shutting down
};

/**
 * @brief Statistics for bot operation
 */
struct BotStats {
    int64_t startTime{0};
    int64_t totalTicks{0};
    int32_t npcKills{0};
    int32_t boxesCollected{0};
    int32_t deaths{0};
    int32_t fleeEvents{0};
    int64_t btcEarned{0};
    
    void reset() {
        startTime = 0;
        totalTicks = 0;
        npcKills = 0;
        boxesCollected = 0;
        deaths = 0;
        fleeEvents = 0;
        btcEarned = 0;
    }
};

enum class ResourceAutomationState {
    Idle,
    WaitingForResourcesInfo,
    TravelingToTradeMap,
    MovingToTradeStation,
    WaitingForTradeInfo,
    ReturningToWorkMap
};

struct ResourceSellStep {
    ResourceType resource{ResourceType::Cerium};
    int32_t amount{0};
};

/**
 * @brief Main bot controller
 * 
 * Owns:
 * - GameEngine reference
 * - Module scheduler
 * - Bot configuration
 * - Running state
 * 
 * Usage:
 *   auto engine = std::make_shared<GameEngine>();
 *   BotController bot(engine, BotConfig{});
 *   bot.start();
 *   // ... bot runs in background
 *   bot.stop();
 */
class BotController {
public:
    BotController(std::shared_ptr<GameEngine> engine, BotConfig config)
        : engine_(std::move(engine))
        , movement_(std::make_shared<MovementController>(engine_))
        , configService_(std::make_shared<ConfigService>())
        , safetySessionState_(std::make_shared<SafetySessionState>()) {
        configService_->adoptLegacyConfig(std::move(config));
        refreshModulesFromConfigIfNeeded(true);
    }

    BotController(std::shared_ptr<GameEngine> engine,
                  std::shared_ptr<ConfigService> configService)
        : engine_(std::move(engine))
        , movement_(std::make_shared<MovementController>(engine_))
        , configService_(configService ? std::move(configService) : std::make_shared<ConfigService>())
        , safetySessionState_(std::make_shared<SafetySessionState>()) {
        refreshModulesFromConfigIfNeeded(true);
    }
    
    ~BotController() {
        stop();
    }
    
    // Non-copyable
    BotController(const BotController&) = delete;
    BotController& operator=(const BotController&) = delete;
    
    /**
     * @brief Start the bot
     * 
     * Begins the main loop in a background thread.
     */
    void start() {
        if (state_ == BotState::Paused) {
            resume();
            return;
        }
        if (state_ != BotState::Stopped) {
            std::cerr << "[BotController] Already running\n";
            return;
        }

        refreshModulesFromConfigIfNeeded(true);
        state_ = BotState::Starting;
        shouldRun_ = true;
        safetySessionState_->reset();
        resetResourceAutomationState();
        stats_.reset();
        stats_.startTime = currentTimeMs();
        
        botThread_ = std::thread([this]() {
            try {
                runLoop();
            } catch (const std::exception& ex) {
                std::cerr << "[BotController] Unhandled exception: " << ex.what() << "\n";
                state_ = BotState::Stopped;
                shouldRun_ = false;
            } catch (...) {
                std::cerr << "[BotController] Unhandled unknown exception\n";
                state_ = BotState::Stopped;
                shouldRun_ = false;
            }
        });
        
        std::cout << "[BotController] Started\n";
    }
    
    /**
     * @brief Stop the bot
     * 
     * Signals the main loop to stop and waits for thread to finish.
     */
    void stop() {
        if (state_ == BotState::Stopped) {
            return;
        }
        
        state_ = BotState::Stopping;
        shouldRun_ = false;
        
        if (botThread_.joinable()) {
            botThread_.join();
        }
        
        state_ = BotState::Stopped;
        safetySessionState_->reset();
        resetResourceAutomationState();
        std::cout << "[BotController] Stopped\n";
    }
    
    /**
     * @brief Pause bot operation
     */
    void pause() {
        if (state_ == BotState::Running) {
            if (movement_) {
                movement_->reset();
            }
            state_ = BotState::Paused;
            if (stateCallback_) {
                stateCallback_(state_);
            }
            std::cout << "[BotController] Paused\n";
        }
    }
    
    /**
     * @brief Resume bot operation
     */
    void resume() {
        if (state_ == BotState::Paused) {
            refreshModulesFromConfigIfNeeded(true);
            state_ = BotState::Running;
            if (stateCallback_) {
                stateCallback_(state_);
            }
            std::cout << "[BotController] Resumed\n";
        }
    }
    
    /**
     * @brief Toggle pause/resume
     */
    void togglePause() {
        if (state_ == BotState::Paused) {
            resume();
        } else if (state_ == BotState::Running) {
            pause();
        }
    }

    void handleImmediateDeathEvent() {
        if (!isRunning()) {
            return;
        }
        // Signal the bot loop to process death — avoids thread-safety issues
        // since this callback runs on the engine's network thread.
        immediateDeathRevivePending_.store(true);
    }
    
    void handleReviveEvent() {
        if (!isRunning()) {
            return;
        }
        reviveEventPending_.store(true);
    }

    // Getters
    [[nodiscard]] BotState getState() const noexcept { return state_; }
    [[nodiscard]] bool isRunning() const noexcept { return state_ == BotState::Running; }
    [[nodiscard]] bool isPaused() const noexcept { return state_ == BotState::Paused; }
    
    [[nodiscard]] BotConfig getConfig() const { 
        if (auto snapshot = configService_->snapshot()) {
            return snapshot->runtime;
        }
        return BotConfig{};
    }
    
    [[nodiscard]] BotStats getStats() const { 
        std::lock_guard<std::mutex> lock(statsMutex_);
        return stats_; 
    }
    
    [[nodiscard]] const Scheduler& getScheduler() const noexcept { return scheduler_; }
    [[nodiscard]] std::shared_ptr<ConfigService> getConfigService() const noexcept {
        return configService_;
    }
    [[nodiscard]] std::vector<BotProfile> getProfiles() const {
        return configService_->profiles();
    }
    [[nodiscard]] std::string activeProfileId() const {
        return configService_->activeProfileId();
    }
    bool selectProfile(const std::string& profileId, std::string* error = nullptr) {
        const bool changed = configService_->setActiveProfile(profileId, error);
        if (changed && state_ == BotState::Stopped) {
            refreshModulesFromConfigIfNeeded(true);
        }
        return changed;
    }
    [[nodiscard]] std::optional<CombatTelemetry> getCombatTelemetry() const {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        Module* combatBase = scheduler_.findModule("Combat");
        auto* combat = dynamic_cast<CombatModule*>(combatBase);
        if (!combat) {
            return std::nullopt;
        }
        return combat->getTelemetry();
    }
    [[nodiscard]] std::optional<SafetyTelemetry> getSafetyTelemetry() const {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        Module* safetyBase = scheduler_.findModule("Safety");
        auto* safety = dynamic_cast<SafetyModule*>(safetyBase);
        if (!safety) {
            return std::nullopt;
        }
        return safety->getTelemetry();
    }
    [[nodiscard]] std::optional<TravelTelemetry> getTravelTelemetry() const {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        Module* travelBase = scheduler_.findModule("Travel");
        auto* travel = dynamic_cast<TravelModule*>(travelBase);
        if (!travel) {
            return std::nullopt;
        }
        return travel->getTelemetry();
    }
    [[nodiscard]] std::optional<RoamingTelemetry> getRoamingTelemetry() const {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        Module* roamingBase = scheduler_.findModule("Roaming");
        auto* roaming = dynamic_cast<RoamingModule*>(roamingBase);
        if (!roaming) {
            return std::nullopt;
        }
        return roaming->getTelemetry();
    }
    
    [[nodiscard]] BotMode getMode() const { 
        if (auto snapshot = configService_->snapshot()) {
            return snapshot->runtime.mode;
        }
        return BotMode::KillCollect;
    }
    
    /**
     * @brief Update configuration
     */
    void setConfig(BotConfig config) {
        configService_->adoptLegacyConfig(std::move(config));
        if (state_ == BotState::Stopped) {
            refreshModulesFromConfigIfNeeded(true);
        }
    }

    /**
     * @brief Update only bot mode (kill / collect / kill&collect)
     */
    void setMode(BotMode mode) {
        configService_->setMode(mode);
        if (state_ == BotState::Stopped) {
            refreshModulesFromConfigIfNeeded(true);
        }
    }

    /**
     * @brief Update working map target
     */
    void setWorkingMap(std::string mapName) {
        configService_->setWorkingMap(mapName);
        if (state_ == BotState::Stopped) {
            refreshModulesFromConfigIfNeeded(true);
        }
    }
    
    /**
     * @brief Set callback for state changes
     */
    void setStateCallback(std::function<void(BotState)> callback) {
        stateCallback_ = std::move(callback);
    }

private:
    void applyModeToModulesLocked(const BotConfig& runtime) {
        Module* combat = scheduler_.findModule("Combat");
        Module* collect = scheduler_.findModule("Collect");
        Module* roaming = scheduler_.findModule("Roaming");

        const bool combatOn = runtime.mode == BotMode::Kill || runtime.mode == BotMode::KillCollect;
        const bool collectOn = runtime.mode == BotMode::Collect || runtime.mode == BotMode::KillCollect;

        if (combat) combat->setEnabled(combatOn);
        if (collect) collect->setEnabled(collectOn);
        if (roaming) roaming->setEnabled(combatOn || collectOn);
    }

    std::shared_ptr<GameEngine> engine_;
    std::shared_ptr<MovementController> movement_;
    std::shared_ptr<ConfigService> configService_;
    std::shared_ptr<SafetySessionState> safetySessionState_;
    Scheduler scheduler_;
    BotStats stats_;
    
    mutable std::mutex statsMutex_;
    mutable std::mutex schedulerMutex_;
    
    std::atomic<BotState> state_{BotState::Stopped};
    std::atomic<bool> shouldRun_{false};
    std::atomic<bool> immediateDeathRevivePending_{false};
    std::atomic<bool> reviveEventPending_{false};
    std::thread botThread_;
    
    std::function<void(BotState)> stateCallback_;
    bool wasDead_{false};
    int64_t deathDetectedAtMs_{0};
    int64_t nextReviveAttemptAtMs_{0};
    uint64_t appliedConfigVersion_{0};
    bool reconnectPending_{false};
    int64_t reconnectAtMs_{0};
    std::string reconnectReason_;
    ResourceAutomationState resourceAutomationState_{ResourceAutomationState::Idle};
    ResourceAutomationPlan lastResourcePlan_;
    MapGraph resourceMapGraph_;
    int64_t lastResourceInfoRequestAtMs_{0};
    int64_t lastTradeInfoRequestAtMs_{0};
    int64_t nextResourceActionAtMs_{0};
    int64_t resourceFlowStartedAtMs_{0};
    int64_t resourceIdleBackoffUntilMs_{0};
    int64_t lastRefineCompletedAtMs_{0};
    bool resourceFlowTriggeredByCargo_{false};
    std::string resourceTradeTargetMap_;

    // Autobuy
    int64_t lastAutobuyCheckMs_{0};
    bool autobuyBuyInProgress_{false};
    static constexpr int64_t AUTOBUY_CHECK_INTERVAL_MS = 10000;
    static constexpr int32_t LASER_BUY_AMOUNT = 10000;
    static constexpr int32_t LASER_MIN_AMOUNT = 10000;
    static constexpr int32_t ROCKET_BUY_AMOUNT = 1000;
    static constexpr int32_t ROCKET_MIN_AMOUNT = 1000;
    static constexpr int64_t RESOURCE_INFO_RETRY_MS = 2000;
    static constexpr int64_t RESOURCE_INFO_TIMEOUT_MS = 5000;
    static constexpr int64_t RESOURCE_ACTION_COOLDOWN_MS = 600;
    static constexpr int64_t TRADE_STATION_ARRIVAL_RANGE = 260;
    static constexpr int64_t RESOURCE_FLOW_GLOBAL_TIMEOUT_MS = 5 * 60 * 1000;
    static constexpr int64_t RESOURCE_IDLE_BACKOFF_MS = 30000;
    static constexpr std::array<std::string_view, 6> TRADE_MAP_NAMES = {
        "R-1", "E-1", "U-1", "R-7", "E-7", "U-7"
    };

    void tickAutobuy(const GameSnapshot& snap, const AutobuyConfig& config) {
        if (!config.anyEnabled()) return;
        if (engine_->state() != EngineState::InGame) return;

        const auto now = snap.timestampMs;
        if (now - lastAutobuyCheckMs_ < AUTOBUY_CHECK_INTERVAL_MS) return;
        lastAutobuyCheckMs_ = now;

        // Fetch catalog if not loaded yet
        if (!engine_->hasShopCatalog()) {
            engine_->requestShopItems();
            return;
        }

        const auto& catalog = engine_->shopCatalog();
        const auto& hero = snap.hero;

        auto findItem = [&](const std::string& title) -> const GameEngine::ShopItem* {
            for (const auto& item : catalog) {
                if (item.title == title) return &item;
            }
            return nullptr;
        };

        auto tryBuyLaser = [&](bool enabled, int32_t currentAmount, const std::string& title) {
            if (!enabled || currentAmount >= LASER_MIN_AMOUNT) return;
            const auto* item = findItem(title);
            if (!item) return;
            int32_t batchCount = LASER_BUY_AMOUNT / std::max(item->quantity, 1);
            int32_t totalPrice = batchCount * item->price;
            bool isPlt = item->currencyKindId == "currency_2";
            if (isPlt && hero.plt < totalPrice) return;
            if (!isPlt && hero.btc < totalPrice) return;
            engine_->buyShopItem(item->itemId, LASER_BUY_AMOUNT, totalPrice);
        };

        auto tryBuyRocket = [&](bool enabled, int32_t currentAmount, const std::string& title) {
            if (!enabled || currentAmount >= ROCKET_MIN_AMOUNT) return;
            const auto* item = findItem(title);
            if (!item) return;
            int32_t batchCount = ROCKET_BUY_AMOUNT / std::max(item->quantity, 1);
            int32_t totalPrice = batchCount * item->price;
            bool isPlt = item->currencyKindId == "currency_2";
            if (isPlt && hero.plt < totalPrice) return;
            if (!isPlt && hero.btc < totalPrice) return;
            engine_->buyShopItem(item->itemId, ROCKET_BUY_AMOUNT, totalPrice);
        };

        tryBuyLaser(config.laserRlx1, hero.lasers.rlx1, "RLX-1");
        tryBuyLaser(config.laserGlx2, hero.lasers.glx2, "GLX-2");
        tryBuyLaser(config.laserBlx3, hero.lasers.blx3, "BLX-3");
        tryBuyLaser(config.laserGlx2As, hero.lasers.glx2as, "GLX-2-AS");
        tryBuyLaser(config.laserMrs6x, hero.lasers.mrs6x, "MRS-6X");
        tryBuyRocket(config.rocketKep410, hero.rockets.kep410, "KEP-410");
        tryBuyRocket(config.rocketNc30, hero.rockets.nc30, "NC-30");
        tryBuyRocket(config.rocketTnc130, hero.rockets.tnc130, "TNC-130");
    }

    void initializeModules(const BotConfig& runtime) {
        scheduler_.clear();

        // Add modules in order (doesn't affect priority, just iteration order)
        // Priority order: Safety (90) > Travel (75) > Combat (60) > Collect (40) > Roaming (10)
        scheduler_.addModule(std::make_unique<SafetyModule>(
            engine_,
            movement_,
            runtime.safety,
            runtime.map,
            runtime.admin,
            safetySessionState_
        ));
        scheduler_.addModule(std::make_unique<TravelModule>(engine_, movement_, runtime.map));
        scheduler_.addModule(std::make_unique<CombatModule>(
            engine_,
            movement_,
            runtime.combat,
            runtime.collect
        ));
        scheduler_.addModule(std::make_unique<CollectModule>(engine_, movement_, runtime.collect));
        scheduler_.addModule(std::make_unique<RoamingModule>(engine_, movement_, runtime.roaming));
        applyModeToModulesLocked(runtime);
        std::cout << "[BotController] Initialized " << scheduler_.moduleCount() << " modules\n";
    }

    void refreshModulesFromConfigIfNeeded(bool force = false) {
        const auto snapshot = configService_->snapshot();
        if (!snapshot) {
            return;
        }
        if (!force && snapshot->version == appliedConfigVersion_) {
            return;
        }
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        if (movement_) {
            movement_->reset();
        }
        initializeModules(snapshot->runtime);
        appliedConfigVersion_ = snapshot->version;
    }
    
    void runLoop() {
        state_ = BotState::Running;
        wasDead_ = false;
        deathDetectedAtMs_ = 0;
        nextReviveAttemptAtMs_ = 0;
        immediateDeathRevivePending_.store(false);
        reviveEventPending_.store(false);
        resetResourceAutomationState();
        
        if (stateCallback_) {
            stateCallback_(state_);
        }
        
        auto lastTick = std::chrono::steady_clock::now();
        
        while (shouldRun_) {
            auto configSnapshot = configService_->snapshot();
            BotConfig runtime = configSnapshot ? configSnapshot->runtime : BotConfig{};
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick);
            
            // Maintain tick rate
            if (elapsed.count() < runtime.tickRateMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(runtime.tickRateMs - elapsed.count()));
                continue;
            }
            
            lastTick = now;
            refreshModulesFromConfigIfNeeded();
            configSnapshot = configService_->snapshot();
            runtime = configSnapshot ? configSnapshot->runtime : BotConfig{};
            
            // Skip tick if paused
            if (state_ == BotState::Paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (!reconnectPending_ &&
                (engine_->state() == EngineState::Disconnected ||
                 engine_->state() == EngineState::Error ||
                 engine_->state() == EngineState::NotConnected) &&
                appliedConfigVersion_ != 0) {
                scheduleReconnectAttempt(
                    currentTimeMs(),
                    3000,
                    "UnexpectedDisconnect"
                );
            }

            processReconnectCooldown(currentTimeMs());
            if (reconnectPending_ || !engine_->isConnected()) {
                continue;
            }
            
            // Get game state snapshot
            GameSnapshot snap = createSnapshot(runtime);

            // Global gates and lifecycle handling
            const bool deadNow = handleDeathsAndRevive(snap, runtime);
            if (reconnectPending_ || deadNow) {
                continue;
            }
            const bool resourceConsumedTick = handleResourceAutomation(snap, runtime);
            if (!resourceConsumedTick) {
                enforceWorkingMapGates(snap, runtime);
                applyModeToModules(runtime);

                // Run scheduler tick
                {
                    std::lock_guard<std::mutex> lock(schedulerMutex_);
                    scheduler_.tick(snap);
                }
            }
            handleDisconnectPolicies(snap, runtime);
            tickAutobuy(snap, runtime.autobuy);

            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.totalTicks++;
        }
        
        state_ = BotState::Stopped;
        
        if (stateCallback_) {
            stateCallback_(state_);
        }
    }
    
    [[nodiscard]] GameSnapshot createSnapshot(const BotConfig& runtime) const {
        GameSnapshot snap;
        
        // Get snapshots from engine
        snap.hero = engine_->hero();
        snap.entities = engine_->entities();
        snap.resources = engine_->resourceState();
        snap.timestampMs = currentTimeMs();
        snap.mapName = engine_->currentMap();
        snap.mapId = engine_->mapInfo().id;
        snap.inSafeZone = snap.hero.inSafeZone;
        snap.mode = runtime.mode;
        
        return snap;
    }
    
    [[nodiscard]] static int64_t currentTimeMs() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
    }

    void resetSafetyAfterReviveRequest(int64_t nowMs) {
        engine_->clearPendingActions();
        if (movement_) {
            movement_->reset();
        }

        std::lock_guard<std::mutex> lock(schedulerMutex_);
        Module* safetyBase = scheduler_.findModule("Safety");
        auto* safety = dynamic_cast<SafetyModule*>(safetyBase);
        if (safety) {
            safety->resetAfterRevive(nowMs);
        }
    }

    void confirmSafetyAfterRevive(int64_t nowMs) {
        engine_->clearPendingActions();
        if (movement_) {
            movement_->reset();
        }

        std::lock_guard<std::mutex> lock(schedulerMutex_);
        Module* safetyBase = scheduler_.findModule("Safety");
        auto* safety = dynamic_cast<SafetyModule*>(safetyBase);
        if (safety) {
            safety->confirmRevive(nowMs);
        }
    }

    [[nodiscard]] static bool isTradeMapName(const std::string& mapName) {
        for (const auto candidate : TRADE_MAP_NAMES) {
            if (mapName == candidate) {
                return true;
            }
        }
        return false;
    }

    void logResourceFlow(const std::string& message) const {
        std::cout << "[ResourcesFlow] " << message << "\n";
    }

    void resetResourceAutomationState() {
        resourceAutomationState_ = ResourceAutomationState::Idle;
        lastResourcePlan_ = ResourceAutomationPlan{};
        lastResourceInfoRequestAtMs_ = 0;
        lastTradeInfoRequestAtMs_ = 0;
        nextResourceActionAtMs_ = 0;
        resourceFlowStartedAtMs_ = 0;
        resourceIdleBackoffUntilMs_ = 0;
        resourceFlowTriggeredByCargo_ = false;
        resourceTradeTargetMap_.clear();
    }

    void suspendSchedulerForControllerWork(bool resetMovement = true) {
        {
            std::lock_guard<std::mutex> lock(schedulerMutex_);
            scheduler_.suspendCurrentModule();
        }
        if (resetMovement && movement_) {
            movement_->reset();
        }
    }

    void cancelTravelModule() {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        Module* travelBase = scheduler_.findModule("Travel");
        auto* travel = dynamic_cast<TravelModule*>(travelBase);
        if (travel && (!travel->getDestination().empty() || travel->isTraveling())) {
            travel->cancelTravel();
        }
    }

    void enterResourcesInfoWait(const GameSnapshot& snap,
                                std::string_view reason,
                                int64_t delayMs = 0) {
        if (resourceFlowStartedAtMs_ <= 0) {
            resourceFlowStartedAtMs_ = snap.timestampMs;
        }
        resourceAutomationState_ = ResourceAutomationState::WaitingForResourcesInfo;
        lastResourceInfoRequestAtMs_ = 0;
        nextResourceActionAtMs_ = snap.timestampMs + std::max<int64_t>(0, delayMs);
        if (!reason.empty()) {
            logResourceFlow(std::string(reason));
        }
    }

    void enterTradeInfoWait(const GameSnapshot& snap,
                            std::string_view reason,
                            int64_t delayMs = 0) {
        resourceAutomationState_ = ResourceAutomationState::WaitingForTradeInfo;
        lastTradeInfoRequestAtMs_ = 0;
        nextResourceActionAtMs_ = snap.timestampMs + std::max<int64_t>(0, delayMs);
        if (!reason.empty()) {
            logResourceFlow(std::string(reason));
        }
    }

    void finishResourceAutomation([[maybe_unused]] const GameSnapshot& snap,
                                  [[maybe_unused]] const BotConfig& runtime,
                                  std::string_view reason) {
        if (resourceAutomationState_ == ResourceAutomationState::TravelingToTradeMap ||
            resourceAutomationState_ == ResourceAutomationState::ReturningToWorkMap) {
            cancelTravelModule();
        }

        if (!reason.empty()) {
            logResourceFlow(std::string(reason));
        }

        resetResourceAutomationState();
    }

    bool beginReturnToWorkingMapIfNeeded(const GameSnapshot& snap,
                                         const BotConfig& runtime,
                                         std::string_view reason) {
        if (!runtime.map.workingMap.empty() &&
            snap.mapName != runtime.map.workingMap &&
            isTradeMapName(snap.mapName)) {
            cancelTravelModule();
            resourceAutomationState_ = ResourceAutomationState::ReturningToWorkMap;
            resourceTradeTargetMap_.clear();
            if (!reason.empty()) {
                logResourceFlow(std::string(reason));
            }
            return true;
        }

        finishResourceAutomation(snap, runtime, reason);
        return false;
    }

    [[nodiscard]] bool shouldYieldResourceAutomationToScheduler(const GameSnapshot& snap,
                                                                const BotConfig& runtime) const {
        if (!runtime.safety.enabled) {
            return false;
        }

        if (snap.hero.healthPercent() < static_cast<float>(runtime.safety.repairHpPercent)) {
            return true;
        }

        for (const auto& enemy : snap.entities.enemies) {
            if (enemy.isAttacking && enemy.selectedTarget == snap.hero.id) {
                return true;
            }
        }

        if (runtime.safety.fleeMode == SafetyFleeMode::OnEnemySeen &&
            !snap.entities.enemies.empty()) {
            return true;
        }

        const auto safety = getSafetyTelemetry();
        if (!safety.has_value()) {
            return false;
        }

        if (safety->state != SafetyState::Monitoring ||
            safety->beingAttacked ||
            safety->attackers > 0 ||
            safety->adminSeenRecently) {
            return true;
        }

        return runtime.safety.fleeMode == SafetyFleeMode::OnEnemySeen &&
               safety->visibleEnemies > 0;
    }

    [[nodiscard]] static char factionPrefix(int32_t fraction) {
        switch (fraction) {
            case 1: return 'R';
            case 2: return 'E';
            case 3: return 'U';
            default: return '\0';
        }
    }

    [[nodiscard]] std::optional<std::string> selectNearestTradeMap(const GameSnapshot& snap,
                                                                   const BotConfig& runtime) const {
        const std::unordered_set<std::string> blocked(runtime.map.avoidMaps.begin(),
                                                      runtime.map.avoidMaps.end());
        const char ownPrefix = factionPrefix(snap.hero.fraction);

        std::optional<std::string> bestMap;
        int bestDistance = std::numeric_limits<int>::max();

        for (const auto candidateView : TRADE_MAP_NAMES) {
            if (ownPrefix != '\0' && !candidateView.empty() && candidateView[0] != ownPrefix) {
                continue;
            }

            const std::string candidate(candidateView);
            int distance = 0;

            if (snap.mapName != candidate) {
                const auto path = resourceMapGraph_.findPath(snap.mapName, candidate, blocked);
                if (path.empty()) {
                    continue;
                }
                distance = static_cast<int>(path.size());
            }

            if (distance < bestDistance) {
                bestDistance = distance;
                bestMap = candidate;
            }
        }

        return bestMap;
    }

    [[nodiscard]] const StationInfo* nearestTradeStation(const GameSnapshot& snap) const {
        const StationInfo* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();

        for (const auto& station : snap.entities.stations) {
            if (!station.isTradeStation()) {
                continue;
            }

            const float distance = station.distanceTo(snap.hero.x, snap.hero.y);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = &station;
            }
        }

        return best;
    }

    [[nodiscard]] std::vector<ResourceSellStep> buildSellSteps(
        const ResourceStateSnapshot& resources,
        const ResourceAutomationSettings& settings) const {
        static constexpr std::array<ResourceType, kResourceTypeCount> sellOrder = {
            ResourceType::Cerium,
            ResourceType::Mercury,
            ResourceType::Erbium,
            ResourceType::Piritid,
            ResourceType::Darkonit,
            ResourceType::Uranit,
            ResourceType::Azurit,
            ResourceType::Dungid,
            ResourceType::Xureon,
        };

        std::unordered_set<int32_t> selectedMaterials;
        auto markSelected = [&](const ResourceModuleSettings& module) {
            if (module.enabled) {
                selectedMaterials.insert(static_cast<int32_t>(toResourceType(module.material)));
            }
        };
        markSelected(settings.speed);
        markSelected(settings.shields);
        markSelected(settings.lasers);
        markSelected(settings.rockets);

        std::vector<ResourceSellStep> steps;
        steps.reserve(kResourceTypeCount);

        for (const auto resource : sellOrder) {
            const auto type = static_cast<int32_t>(resource);
            if (selectedMaterials.contains(type)) {
                continue;
            }

            const auto* stack = resources.findTradeResource(type);
            if (!stack || stack->amount <= 0) {
                continue;
            }

            steps.push_back(ResourceSellStep{resource, stack->amount});
        }
        return steps;
    }

    bool runTravelOnlySchedulerTick(const GameSnapshot& snap, const std::string& destination) {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        auto* travel = dynamic_cast<TravelModule*>(scheduler_.findModule("Travel"));
        if (!travel) {
            return false;
        }

        if (travel->getDestination() != destination) {
            travel->setDestination(destination);
        }

        if (Module* combat = scheduler_.findModule("Combat")) combat->setEnabled(false);
        if (Module* collect = scheduler_.findModule("Collect")) collect->setEnabled(false);
        if (Module* roaming = scheduler_.findModule("Roaming")) roaming->setEnabled(false);
        if (Module* travelBase = scheduler_.findModule("Travel")) travelBase->setEnabled(true);

        scheduler_.tick(snap);
        return true;
    }

    bool executeRefineStep(const GameSnapshot& snap,
                           const ResourceRefinePlanStep& step,
                           std::string_view kind) {
        const auto* stack = snap.resources.findResource(static_cast<int32_t>(step.target));
        if (!stack) {
            logResourceFlow("Refine skipped because target stack is missing in resource snapshot.");
            enterResourcesInfoWait(snap, "Retry resource refresh after missing refine target.");
            return true;
        }

        const int32_t maxChunk = stack->maxRefineAmount > 0 ? stack->maxRefineAmount : step.amount;
        const int32_t amount = std::min(step.amount, maxChunk);
        if (amount <= 0) {
            logResourceFlow(std::string(kind) + " refine for " + resourceTypeName(step.target) +
                            " is currently unavailable; waiting for fresh panel data.");
            enterResourcesInfoWait(snap, "Waiting for refine availability.");
            return true;
        }

        suspendSchedulerForControllerWork();
        engine_->clearPendingActions();
        engine_->refineResource(static_cast<int32_t>(step.target), amount);
        logResourceFlow(std::string("Sent ") + std::string(kind) + " refine: " +
                        std::to_string(amount) + " " + resourceTypeName(step.target));
        enterResourcesInfoWait(snap, "Waiting for resources refresh after refine.", RESOURCE_ACTION_COOLDOWN_MS);
        return true;
    }

    bool executeEnrichStep(const GameSnapshot& snap,
                           const ResourceEnrichPlanStep& step) {
        if (step.amount <= 0) {
            enterResourcesInfoWait(snap, "Skipping empty enrich step.");
            return true;
        }

        suspendSchedulerForControllerWork();
        engine_->clearPendingActions();
        engine_->enrichModule(static_cast<int32_t>(step.module),
                              static_cast<int32_t>(step.material),
                              step.amount);
        logResourceFlow(std::string("Sent enrich: ") +
                        resourceModuleTypeName(step.module) +
                        " <- " + resourceTypeName(step.material) +
                        " amount " + std::to_string(step.amount));
        enterResourcesInfoWait(snap, "Waiting for resources refresh after enrich.", RESOURCE_ACTION_COOLDOWN_MS);
        return true;
    }

    bool executeSellStep(const GameSnapshot& snap,
                         const ResourceSellStep& step) {
        if (step.amount <= 0) {
            enterTradeInfoWait(snap, "Skipping empty sell step.");
            return true;
        }

        suspendSchedulerForControllerWork();
        engine_->clearPendingActions();
        engine_->sellResource(static_cast<int32_t>(step.resource), step.amount);
        logResourceFlow(std::string("Sold ") + std::to_string(step.amount) +
                        " " + resourceTypeName(step.resource));
        enterTradeInfoWait(snap, "Waiting for trade refresh after sell.", RESOURCE_ACTION_COOLDOWN_MS);
        return true;
    }

    bool handleResourceAutomation(const GameSnapshot& snap, const BotConfig& runtime) {
        if (!runtime.resources.enabled) {
            if (resourceAutomationState_ != ResourceAutomationState::Idle) {
                finishResourceAutomation(snap, runtime, "Resource automation disabled; controller state cleared.");
            }
            return false;
        }

        const bool cargoFull = snap.hero.isCargoFull();
        const int64_t refineIntervalMs = static_cast<int64_t>(runtime.resources.refineIntervalSeconds) * 1000;
        const bool periodicRefineReady =
            lastRefineCompletedAtMs_ == 0 ||
            (snap.timestampMs - lastRefineCompletedAtMs_ >= refineIntervalMs);

        if (!cargoFull &&
            resourceAutomationState_ != ResourceAutomationState::ReturningToWorkMap) {
            if (resourceAutomationState_ != ResourceAutomationState::Idle) {
                if (resourceFlowTriggeredByCargo_) {
                    resourceIdleBackoffUntilMs_ = 0;
                    return beginReturnToWorkingMapIfNeeded(
                        snap,
                        runtime,
                        "Cargo no longer full; resource workflow finished.");
                }
            }
        }

        if (resourceAutomationState_ == ResourceAutomationState::Idle) {
            if (snap.timestampMs < resourceIdleBackoffUntilMs_) {
                return false;
            }
            if (cargoFull) {
                resourceFlowTriggeredByCargo_ = true;
                enterResourcesInfoWait(snap, "Cargo reached 100%; starting resource workflow.");
            } else if (periodicRefineReady) {
                resourceFlowTriggeredByCargo_ = false;
                enterResourcesInfoWait(snap, "Periodic refine/enrich cycle started.");
            } else {
                return false;
            }
        }

        if (resourceFlowStartedAtMs_ > 0 &&
            snap.timestampMs - resourceFlowStartedAtMs_ > RESOURCE_FLOW_GLOBAL_TIMEOUT_MS) {
            finishResourceAutomation(snap, runtime, "Resource flow global timeout; aborting.");
            return false;
        }

        if (shouldYieldResourceAutomationToScheduler(snap, runtime)) {
            return false;
        }

        switch (resourceAutomationState_) {
            case ResourceAutomationState::Idle:
                return false;

            case ResourceAutomationState::WaitingForResourcesInfo: {
                if (lastResourceInfoRequestAtMs_ > 0 &&
                    snap.resources.hasResourcesInfo &&
                    snap.resources.resourcesInfoUpdatedAtMs >= lastResourceInfoRequestAtMs_) {
                    lastResourcePlan_ = ResourcePlanner::build(runtime.resources, snap.resources, cargoFull);

                    if (!lastResourcePlan_.targetedRefineSteps.empty()) {
                        return executeRefineStep(
                            snap,
                            lastResourcePlan_.targetedRefineSteps.front(),
                            "targeted");
                    }

                    if (!lastResourcePlan_.compressionRefineSteps.empty()) {
                        return executeRefineStep(
                            snap,
                            lastResourcePlan_.compressionRefineSteps.front(),
                            "compression");
                    }

                    if (!lastResourcePlan_.enrichSteps.empty()) {
                        return executeEnrichStep(snap, lastResourcePlan_.enrichSteps.front());
                    }

                    if (resourceFlowTriggeredByCargo_ && lastResourcePlan_.needsSellTrip) {
                        if (isTradeMapName(snap.mapName)) {
                            resourceAutomationState_ = ResourceAutomationState::MovingToTradeStation;
                            logResourceFlow("Planner could not free cargo; already on trade map.");
                            return true;
                        }

                        const auto tradeMap = selectNearestTradeMap(snap, runtime);
                        if (!tradeMap.has_value()) {
                            finishResourceAutomation(snap, runtime, "No reachable trade map found; aborting sell trip.");
                            return false;
                        }

                        resourceTradeTargetMap_ = *tradeMap;
                        resourceAutomationState_ = ResourceAutomationState::TravelingToTradeMap;
                        logResourceFlow("Planner requires sell trip; heading to " + resourceTradeTargetMap_ + ".");
                        return true;
                    }

                    lastRefineCompletedAtMs_ = snap.timestampMs;
                    if (resourceFlowTriggeredByCargo_ && cargoFull) {
                        finishResourceAutomation(snap, runtime, "Cargo still full but no more actions possible; backing off 30s.");
                        resourceIdleBackoffUntilMs_ = snap.timestampMs + RESOURCE_IDLE_BACKOFF_MS;
                    } else {
                        finishResourceAutomation(snap, runtime, "Refine/enrich cycle completed.");
                    }
                    return false;
                }

                if (snap.timestampMs < nextResourceActionAtMs_) {
                    return true;
                }

                if (lastResourceInfoRequestAtMs_ == 0 ||
                    snap.timestampMs - lastResourceInfoRequestAtMs_ >= RESOURCE_INFO_RETRY_MS) {
                    suspendSchedulerForControllerWork();
                    engine_->requestResourcesInfo();
                    lastResourceInfoRequestAtMs_ = snap.timestampMs;
                    logResourceFlow("Requested fresh resources info.");
                }
                return true;
            }

            case ResourceAutomationState::TravelingToTradeMap: {
                if (resourceTradeTargetMap_.empty()) {
                    finishResourceAutomation(snap, runtime, "Trade destination missing; aborting sell trip.");
                    return false;
                }

                if (snap.mapName == resourceTradeTargetMap_) {
                    resourceAutomationState_ = ResourceAutomationState::MovingToTradeStation;
                    logResourceFlow("Arrived on trade map " + snap.mapName + ".");
                    return true;
                }

                if (!runTravelOnlySchedulerTick(snap, resourceTradeTargetMap_)) {
                    finishResourceAutomation(snap, runtime, "Travel module missing; cannot execute sell trip.");
                    return false;
                }
                return true;
            }

            case ResourceAutomationState::MovingToTradeStation: {
                if (!isTradeMapName(snap.mapName)) {
                    resourceAutomationState_ = ResourceAutomationState::TravelingToTradeMap;
                    return true;
                }

                const auto* station = nearestTradeStation(snap);
                if (!station) {
                    if (snap.timestampMs - resourceFlowStartedAtMs_ > RESOURCE_INFO_TIMEOUT_MS) {
                        finishResourceAutomation(snap, runtime, "Trade station entity missing; aborting sell trip.");
                        return false;
                    }
                    return true;
                }

                const float distance = station->distanceTo(snap.hero.x, snap.hero.y);
                if (distance <= static_cast<float>(TRADE_STATION_ARRIVAL_RANGE)) {
                    suspendSchedulerForControllerWork();
                    engine_->clearPendingActions();
                    enterTradeInfoWait(snap, "Reached trade station; requesting sell inventory.");
                    return true;
                }

                suspendSchedulerForControllerWork(false);
                if (movement_) {
                    movement_->move("ResourcesFlow",
                                    snap,
                                    Position(station->x, station->y),
                                    MoveIntent::Travel);
                } else {
                    engine_->moveTo(station->x, station->y);
                }
                return true;
            }

            case ResourceAutomationState::WaitingForTradeInfo: {
                if (lastTradeInfoRequestAtMs_ > 0 &&
                    snap.resources.hasTradeInfo &&
                    snap.resources.tradeInfoUpdatedAtMs >= lastTradeInfoRequestAtMs_) {
                    const auto sellSteps = buildSellSteps(snap.resources, runtime.resources);
                    if (!sellSteps.empty()) {
                        return executeSellStep(snap, sellSteps.front());
                    }

                    return beginReturnToWorkingMapIfNeeded(
                        snap,
                        runtime,
                        "Trade inventory emptied; returning to normal flow.");
                }

                if (snap.timestampMs < nextResourceActionAtMs_) {
                    return true;
                }

                if (lastTradeInfoRequestAtMs_ == 0 ||
                    snap.timestampMs - lastTradeInfoRequestAtMs_ >= RESOURCE_INFO_RETRY_MS) {
                    suspendSchedulerForControllerWork();
                    engine_->requestResourcesTradeInfo();
                    lastTradeInfoRequestAtMs_ = snap.timestampMs;
                    logResourceFlow("Requested fresh trade inventory.");
                }
                return true;
            }

            case ResourceAutomationState::ReturningToWorkMap: {
                if (runtime.map.workingMap.empty() || snap.mapName == runtime.map.workingMap) {
                    finishResourceAutomation(snap, runtime, "Returned from trade trip.");
                    return false;
                }

                if (!runTravelOnlySchedulerTick(snap, runtime.map.workingMap)) {
                    finishResourceAutomation(snap, runtime, "Travel module missing; cannot return to working map.");
                    return false;
                }
                return true;
            }

            default:
                return false;
        }
    }

    void applyModeToModules(const BotConfig& runtime) {
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        applyModeToModulesLocked(runtime);
    }

    void enforceWorkingMapGates(const GameSnapshot& snap, const BotConfig& runtime) {
        const bool mapMismatch = !runtime.map.workingMap.empty() && snap.mapName != runtime.map.workingMap;
        std::lock_guard<std::mutex> lock(schedulerMutex_);
        Module* travel = scheduler_.findModule("Travel");
        Module* combat = scheduler_.findModule("Combat");
        Module* collect = scheduler_.findModule("Collect");
        Module* roaming = scheduler_.findModule("Roaming");

        if (travel) {
            travel->setEnabled(runtime.map.enabled && runtime.map.autoTravelToWorkingMap);
        }

        if (!mapMismatch) {
            return;
        }

        if (combat) combat->setEnabled(false);
        if (collect) collect->setEnabled(false);
        if (roaming) roaming->setEnabled(false);
    }

    [[nodiscard]] bool handleDeathsAndRevive(const GameSnapshot& snap, const BotConfig& runtime) {
        const bool deadNow = engine_->isDead();
        const int64_t now = snap.timestampMs;
        bool justRevived = false;

        if (!deadNow && reviveEventPending_.exchange(false)) {
            confirmSafetyAfterRevive(now);
            justRevived = true;
        }

        // Process deferred immediate-death signal from engine callback thread
        if (immediateDeathRevivePending_.exchange(false) && deadNow && runtime.revive.enabled) {
            engine_->clearPendingActions();
            if (movement_) {
                movement_->reset();
            }
            resetSafetyAfterReviveRequest(now);
            engine_->revive();
            nextReviveAttemptAtMs_ = now + std::max<int32_t>(500, runtime.revive.waitAfterReviveMs);
            std::cout << "[BotController] Immediate revive from death event\n";
        }

        if (deadNow && !wasDead_) {
            wasDead_ = true;
            deathDetectedAtMs_ = now;
            nextReviveAttemptAtMs_ = std::max(nextReviveAttemptAtMs_, now);
            engine_->clearPendingActions();
            if (movement_) {
                movement_->reset();
            }

            int32_t deathCount = 0;
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.deaths++;
                deathCount = stats_.deaths;
            }
            const int32_t deathCycleCount =
                runtime.revive.maxDeaths > 0
                    ? ((deathCount - 1) % runtime.revive.maxDeaths) + 1
                    : deathCount;

            if (runtime.revive.disconnectOnMaxDeaths &&
                runtime.revive.maxDeaths > 0 &&
                deathCycleCount == runtime.revive.maxDeaths) {
                scheduleReconnectCooldown(
                    now,
                    runtime.revive.disconnectCooldownMinutes,
                    "DeathThreshold"
                );
                return true;
            }

            if (runtime.revive.maxDeaths > 0 &&
                deathCount >= runtime.revive.maxDeaths &&
                runtime.revive.stopBotOnMaxDeaths) {
                std::cout << "[BotController] Max deaths reached, stopping bot\n";
                shouldRun_ = false;
                return true;
            }
        } else if (!deadNow && wasDead_) {
            wasDead_ = false;
            deathDetectedAtMs_ = 0;
            nextReviveAttemptAtMs_ = 0;
            engine_->clearPendingActions();
            if (movement_) {
                movement_->reset();
            }
        }

        if (deadNow && runtime.revive.enabled && nextReviveAttemptAtMs_ > 0 && !reconnectPending_) {
            if (now >= nextReviveAttemptAtMs_) {
                resetSafetyAfterReviveRequest(now);
                engine_->revive();
                nextReviveAttemptAtMs_ = now + std::max<int32_t>(500, runtime.revive.waitAfterReviveMs);
            }
        }

        // Skip module tick on the revive frame — the hero snapshot still
        // contains the pre-death position so any moveTo would fly the ship
        // back to the old flee target instead of staying at the spawn point.
        if (justRevived) {
            return true;
        }

        return deadNow;
    }

    void handleDisconnectPolicies(const GameSnapshot& snap, const BotConfig& runtime) {
        if (reconnectPending_) {
            return;
        }

        if (runtime.admin.enabled && runtime.admin.disconnectWhenSeen) {
            if (const auto safety = getSafetyTelemetry();
                safety.has_value() && safety->adminSeenRecently) {
                scheduleReconnectCooldown(
                    snap.timestampMs,
                    runtime.admin.disconnectCooldownMinutes,
                    "AdminSeen"
                );
            }
        }
    }

    void scheduleReconnectCooldown(int64_t nowMs,
                                   int32_t cooldownMinutes,
                                   std::string reason) {
        scheduleReconnectAttempt(
            nowMs,
            static_cast<int64_t>(std::max(0, cooldownMinutes)) * 60 * 1000,
            std::move(reason)
        );
        {
            std::lock_guard<std::mutex> lock(schedulerMutex_);
            if (movement_) {
                movement_->reset();
            }
            scheduler_.clear();
        }
        engine_->disconnect();
        std::cout << "[BotController] Disconnected for cooldown: "
                  << reconnectReason_ << " (" << cooldownMinutes << " min)\n";
    }

    void scheduleReconnectAttempt(int64_t nowMs,
                                  int64_t delayMs,
                                  std::string reason) {
        reconnectPending_ = true;
        reconnectReason_ = std::move(reason);
        reconnectAtMs_ = nowMs + std::max<int64_t>(0, delayMs);
    }

    void processReconnectCooldown(int64_t nowMs) {
        if (!reconnectPending_) {
            return;
        }

        if (engine_->state() == EngineState::Connecting ||
            engine_->state() == EngineState::Authenticating ||
            engine_->state() == EngineState::Loading) {
            return;
        }

        if (engine_->isConnected()) {
            reconnectPending_ = false;
            reconnectReason_.clear();
            refreshModulesFromConfigIfNeeded(true);
            return;
        }

        if (nowMs < reconnectAtMs_) {
            return;
        }

        if (engine_->connect()) {
            reconnectPending_ = false;
            reconnectReason_.clear();
            appliedConfigVersion_ = 0;
            std::cout << "[BotController] Reconnect started\n";
            return;
        }

        reconnectAtMs_ = nowMs + 5000;
    }
};

} // namespace dynamo
