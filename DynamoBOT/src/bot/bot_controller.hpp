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
#include "movement_controller.hpp"
#include "safety_module.hpp"
#include "combat_module.hpp"
#include "collect_module.hpp"
#include "roaming_module.hpp"
#include "travel_module.hpp"
#include "../config/config_service.hpp"

#include "../game/game_engine.hpp"
#include "../game/hero.hpp"
#include "../game/entities.hpp"

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <functional>
#include <optional>
#include <string>

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
    std::thread botThread_;
    
    std::function<void(BotState)> stateCallback_;
    bool wasDead_{false};
    int64_t deathDetectedAtMs_{0};
    int64_t nextReviveAttemptAtMs_{0};
    uint64_t appliedConfigVersion_{0};
    bool reconnectPending_{false};
    int64_t reconnectAtMs_{0};
    std::string reconnectReason_;

    // Autobuy
    int64_t lastAutobuyCheckMs_{0};
    bool autobuyBuyInProgress_{false};
    static constexpr int64_t AUTOBUY_CHECK_INTERVAL_MS = 10000;
    static constexpr int32_t LASER_BUY_AMOUNT = 10000;
    static constexpr int32_t LASER_MIN_AMOUNT = 10000;
    static constexpr int32_t ROCKET_BUY_AMOUNT = 1000;
    static constexpr int32_t ROCKET_MIN_AMOUNT = 1000;

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
            enforceWorkingMapGates(snap, runtime);
            applyModeToModules(runtime);
            
            // Run scheduler tick
            {
                std::lock_guard<std::mutex> lock(schedulerMutex_);
                scheduler_.tick(snap);
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
