    SafetyModule(std::shared_ptr<GameEngine> engine,
                 std::shared_ptr<MovementController> movement,
                 const SafetyConfig& config,
                 const MapConfig& mapConfig,
                 const AdminConfig& adminConfig,
                 std::shared_ptr<SafetySessionState> sessionState = std::make_shared<SafetySessionState>())
        : Module(std::move(engine), std::move(movement))
        , config_(config)
        , mapConfig_(mapConfig)
        , adminConfig_(adminConfig)
        , sessionState_(sessionState ? std::move(sessionState) : std::make_shared<SafetySessionState>())
        , threatTracker_(adminConfig.droneCountThreshold) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Safety";
    }

    [[nodiscard]] int getPriority(const GameSnapshot& snap) override {
        if (!config_.enabled || !enabled_) {
            return 0;
        }

        updateThreats(snap);

        const double hpPercent = calculateHpPercent(snap);
        const auto summary = threatTracker_.summarize(
            hpPercent,
            snap.hero.x,
            snap.hero.y,
            snap.timestampMs,
            config_.enemySeenTimeoutMs
        );
        const auto assessment = assessEnemyFlee(snap, summary);

        if (summary.adminNearby || hpPercent < config_.minHpPercent) {
            return config_.priority + 10;
        }

        if (assessment.active) {
            return summary.beingAttacked ? config_.priority : config_.priority - 5;
        }

        if (state_ == SafetyState::Fleeing || state_ == SafetyState::AtPortal) {
            return config_.priority - 5;
        }

        // Must hold maximum priority while waiting for teleport to complete
        if (state_ == SafetyState::AwaitingTeleport) {
            return config_.priority + 10;
        }

        if (state_ == SafetyState::Repairing) {
            // During revive grace period, boost priority so Travel can't override
            if (reviveGraceUntilMs_ > 0 && snap.timestampMs < reviveGraceUntilMs_) {
                return config_.priority + 5;
            }
            return config_.priority - 10;
        }

        if (hpPercent < config_.repairHpPercent) {
            return config_.priority - 10;
        }

        if (threatTracker_.adminSeenRecently(
                snap.timestampMs,
                std::max(config_.adminEscapeDelayMs, adminConfig_.escapeDelayMs))) {
            return config_.priority - 15;
        }

        return 0;
    }

    void onStart() override {
        if (state_ != SafetyState::Repairing) {
            transitionTo(SafetyState::Monitoring, 0);
        }
        clearEscapeAnchor();
        lastFleeDistance_ = std::numeric_limits<double>::max();
        lastProgressTime_ = 0;
        fleeRetargetCount_ = 0;
        clearRepairAnchor();
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_ = SafetyTelemetry{};
            telemetry_.state = state_;
        }
        std::cout << "[Safety] Module activated - assessing threats\n";
    }

    void onStop() override {
        if (movement_) {
            movement_->release(name());
        }
        transitionTo(SafetyState::Monitoring, 0);
        clearEscapeAnchor();
        lastFleeDistance_ = std::numeric_limits<double>::max();
        clearRepairAnchor();
        publishTelemetry(GameSnapshot{}, ThreatSummary{}, "Stopped");
        std::cout << "[Safety] Module deactivated\n";
    }

    void tick(const GameSnapshot& snap) override {
        const double hpPercent = calculateHpPercent(snap);
        const auto summary = threatTracker_.summarize(
            hpPercent,
            snap.hero.x,
            snap.hero.y,
            snap.timestampMs,
            config_.enemySeenTimeoutMs
        );

        switch (state_) {
            case SafetyState::Monitoring:
                handleMonitoring(snap, hpPercent, summary);
                break;

            case SafetyState::Fleeing:
                handleFleeing(snap, hpPercent, summary);
                break;

            case SafetyState::AtPortal:
                handleAtPortal(snap, hpPercent, summary);
                break;

            case SafetyState::AwaitingTeleport:
                handleAwaitingTeleport(snap, hpPercent, summary);
                break;

            case SafetyState::Repairing:
                handleRepairing(snap, hpPercent, summary);
                break;

            case SafetyState::ConfigSwitching:
                handleConfigSwitching(snap, summary);
                break;
        }
    }

    [[nodiscard]] SafetyState getState() const noexcept { return state_; }
    [[nodiscard]] const ThreatTracker& getThreatTracker() const noexcept { return threatTracker_; }
    [[nodiscard]] SafetyTelemetry getTelemetry() const {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        return telemetry_;
    }

    void resetAfterRevive(int64_t nowMs = 0) {
        if (movement_) {
            movement_->release(name());
        }

        threatTracker_.clear();
        // Go directly to Repairing — hero always needs repair after death.
        // Arm grace period immediately so handleRepairing() won't see stale
        // aggressors and start fleeing before confirmRevive() arrives.
        transitionTo(SafetyState::Repairing, nowMs);
        reviveGraceUntilMs_ = nowMs + REVIVE_GRACE_PERIOD_MS;
        repairHoldIssued_ = false;
        clearEscapeAnchor();
        fleeTarget_ = Position{};
        lastFleeDistance_ = std::numeric_limits<double>::max();
        lastMoveTime_ = 0;
        lastProgressTime_ = 0;
        fleeRetargetCount_ = 0;
        clearRepairAnchor();
        teleportFromMap_.clear();
        teleportSentAtMs_ = 0;

        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_ = SafetyTelemetry{};
        telemetry_.state = state_;
        telemetry_.lastStateChangeMs = nowMs;
    }

    void confirmRevive(int64_t nowMs = 0) {
        if (movement_) {
            movement_->release(name());
        }

        transitionTo(SafetyState::Repairing, nowMs);
        reviveGraceUntilMs_ = nowMs + REVIVE_GRACE_PERIOD_MS;
        repairHoldIssued_ = false;
        clearEscapeAnchor();
        clearRepairAnchor();
        fleeTarget_ = Position{};
        lastFleeDistance_ = std::numeric_limits<double>::max();
        lastMoveTime_ = 0;
        lastProgressTime_ = 0;

        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = state_;
        telemetry_.lastStateChangeMs = nowMs;
    }
