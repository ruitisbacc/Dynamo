    CombatModule(std::shared_ptr<GameEngine> engine,
                 std::shared_ptr<MovementController> movement,
                 const CombatConfig& config,
                 const CollectConfig& collectConfig = CollectConfig{})
        : Module(std::move(engine), std::move(movement))
        , config_(config)
        , collectConfig_(collectConfig) {
        compileTargetPatterns();
    }
    
    [[nodiscard]] std::string_view name() const noexcept override {
        return "Combat";
    }
    
    [[nodiscard]] int getPriority(const GameSnapshot& snap) override {
        if (!config_.enabled || !enabled_) {
            return 0;
        }

        if (currentTargetId_ != 0 &&
            (state_ == CombatState::Approaching ||
             state_ == CombatState::Attacking ||
             state_ == CombatState::AwaitingKill)) {
            return config_.priority;
        }

        return findBestTarget(snap).has_value() ? config_.priority : 0;
    }
    
    void onStart() override {
        state_ = CombatState::Searching;
        currentTargetId_ = 0;
        currentMovementMode_ = CombatMovementMode::Adaptive;
        mixedUseKite_ = false;
        mixedMovementSwitchAtMs_ = 0;
        autoRocketEnabled_ = false;
        autoRocketStateKnown_ = false;
        lastAutoRocketToggleTime_ = 0;
        trackedTargetId_ = 0;
        lastTrackedTargetDistance_ = std::numeric_limits<double>::max();
        lastTrackedHeroPos_ = Position{};
        lastProgressTime_ = 0;
        currentTargetAcquiredAtMs_ = 0;
        lastDamageProgressAtMs_ = 0;
        lastCombatCollectTime_ = 0;
        combatCollectBlockedUntilMs_ = 0;
        recoveryUntilMs_ = 0;
        lastRecoveryAtMs_ = 0;
        lastOrbitDirectionChangeAtMs_ = 0;
        currentTargetDurability_ = 0;
        currentTargetRecoveries_ = 0;
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_ = CombatTelemetry{};
            telemetry_.movementMode = currentMovementMode_;
            telemetry_.lastModeChangeMs = 0;
        }
        std::cout << "[Combat] Module activated - searching for targets\n";
    }
    
    void onStop() override {
        stopCombatActions(true, true);
        state_ = CombatState::Searching;
        currentTargetId_ = 0;
        currentMovementMode_ = CombatMovementMode::Adaptive;
        mixedUseKite_ = false;
        mixedMovementSwitchAtMs_ = 0;
        autoRocketEnabled_ = false;
        autoRocketStateKnown_ = false;
        lastAutoRocketToggleTime_ = 0;
        trackedTargetId_ = 0;
        lastTrackedTargetDistance_ = std::numeric_limits<double>::max();
        currentTargetAcquiredAtMs_ = 0;
        lastDamageProgressAtMs_ = 0;
        lastCombatCollectTime_ = 0;
        combatCollectBlockedUntilMs_ = 0;
        recoveryUntilMs_ = 0;
        lastOrbitDirectionChangeAtMs_ = 0;
        currentTargetDurability_ = 0;
        currentTargetRecoveries_ = 0;
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_.state = CombatState::Searching;
            telemetry_.movementDecision = "Stopped";
            telemetry_.targetId = 0;
            telemetry_.targetName.clear();
            telemetry_.targetDistance = 0.0f;
            telemetry_.stuckRecoveryActive = false;
        }
        std::cout << "[Combat] Module deactivated\n";
    }
    
    void tick(const GameSnapshot& snap) override {
        ensureCombatConfig(snap);
        pruneTargetLockouts(snap.timestampMs);

        switch (state_) {
            case CombatState::Searching:
                handleSearching(snap);
                break;
                
            case CombatState::Approaching:
                handleApproaching(snap);
                break;
                
            case CombatState::Attacking:
                handleAttacking(snap);
                break;
                
            case CombatState::AwaitingKill:
                handleAwaitingKill(snap);
                break;
                
            case CombatState::Cooldown:
                handleCooldown(snap);
                break;
        }
        
        // Anti-ban random movement
        if (config_.randomMovement &&
            state_ == CombatState::AwaitingKill &&
            recoveryUntilMs_ <= snap.timestampMs) {
            handleAntibanMovement(snap);
        }
    }
    
    [[nodiscard]] CombatState getState() const noexcept { return state_; }
    [[nodiscard]] int32_t getCurrentTargetId() const noexcept { return currentTargetId_; }
    [[nodiscard]] CombatTelemetry getTelemetry() const {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        return telemetry_;
    }
