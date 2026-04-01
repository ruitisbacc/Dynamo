    void beginTarget(const GameSnapshot& snap, const CombatTargetSelection& selection) {
        currentTargetId_ = selection.ship->id;
        currentAmmoType_ = selection.config->ammoType;
        currentRocketType_ = selection.config->rocketType;
        currentMovementMode_ = CombatMovementMode::Adaptive;
        mixedUseKite_ = false;
        mixedMovementSwitchAtMs_ = 0;

        resetTracking(snap, selection.ship);
        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_.movementMode = currentMovementMode_;
            telemetry_.lastModeChangeMs = snap.timestampMs;
            telemetry_.recoveryReason.clear();
        }

        std::cout << "[Combat] Found target: " << selection.ship->name
                  << " (id=" << currentTargetId_ << ", movement="
                  << movementModeName(currentMovementMode_) << ")\n";

        engine_->lockTarget(currentTargetId_);
        lastSelectTime_ = snap.timestampMs;
        state_ = CombatState::Approaching;
        publishTelemetry(snap, selection.ship, "TargetAcquired");
    }
    
    void handleSearching(const GameSnapshot& snap) {
        auto selection = findBestTarget(snap);
        if (!selection.has_value()) {
            publishTelemetry(snap, nullptr, "Searching");
            return;
        }

        beginTarget(snap, *selection);
    }
    
    void handleApproaching(const GameSnapshot& snap) {
        const ShipInfo* target = getCurrentTarget(snap);
        if (!target) {
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "TargetLost");
            return;
        }

        const NpcTargetConfig* targetConfig = matchTargetConfig(target->name);
        if (!targetConfig || !isCurrentTargetValid(snap, *target, *targetConfig)) {
            applyTargetLockout(currentTargetId_, snap.timestampMs, "invalid_target", 7000);
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "InvalidTarget");
            return;
        }

        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position targetPos(target->x, target->y);
        const double dist = heroPos.distanceTo(targetPos);

        if (now - lastSelectTime_ >= config_.selectCooldownMs) {
            lastSelectTime_ = now;
            syncWeaponLoadout(snap, *target, now);
        }

        if (maybeHandleStuckRecovery(snap, target, dist, 2200, "approach_stalled")) {
            if (currentTargetRecoveries_ > MAX_TARGET_RECOVERIES) {
                applyTargetLockout(currentTargetId_, now, "approach_stalled");
                stopCombatActions(true, true);
                clearCurrentTargetState();
                state_ = CombatState::Searching;
                publishTelemetry(snap, nullptr, "ApproachLockout");
            }
            return;
        }

        if (currentTargetAcquiredAtMs_ > 0 &&
            now - currentTargetAcquiredAtMs_ > APPROACH_TIMEOUT_MS) {
            applyTargetLockout(currentTargetId_, now, "approach_timeout");
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "ApproachTimeout");
            return;
        }

        if (dist <= config_.attackRange && snap.hero.selectedTarget == currentTargetId_) {
            state_ = CombatState::Attacking;
            lastAttackTime_ = 0;
            std::cout << "[Combat] In range, engaging " << target->name << "\n";
            publishTelemetry(snap, target, "ReadyToAttack");
            return;
        }

        if (now - lastMoveTime_ >= config_.moveCooldownMs) {
            lastMoveTime_ = now;

            const double heroSpd = static_cast<double>(std::max(snap.hero.speed, 250));
            const double npcSpd = target->observedSpeedEma > 10.0f
                ? std::clamp(static_cast<double>(target->observedSpeedEma), 90.0, 520.0)
                : (target->speed > 0
                    ? std::clamp(static_cast<double>(target->speed), 90.0, 520.0)
                    : 0.0);
            const double approachRange = std::min(
                adaptiveCombatRange(target, snap),
                std::max(160.0, static_cast<double>(config_.attackRange) - ATTACK_READY_MARGIN)
            );
            const OrbitSolution approachSolution = selectApproachEntrySolution(
                snap,
                *target,
                approachRange,
                heroSpd,
                npcSpd,
                orbitClockwise_
            );
            adoptOrbitDirection(approachSolution, now);

            moveWithinBounds(snap, approachSolution.point, MoveIntent::Pursuit);
        }
        publishTelemetry(
            snap,
            target,
            snap.hero.selectedTarget == currentTargetId_ ? "ClosingForAttack" : "LockingTarget"
        );
    }
    
    void handleAttacking(const GameSnapshot& snap) {
        const ShipInfo* target = getCurrentTarget(snap);
        if (!target) {
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Cooldown;
            publishTelemetry(snap, nullptr, "TargetGone");
            return;
        }

        const NpcTargetConfig* targetConfig = matchTargetConfig(target->name);
        if (!targetConfig || !isCurrentTargetValid(snap, *target, *targetConfig)) {
            applyTargetLockout(currentTargetId_, snap.timestampMs, "invalid_target", 7000);
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "InvalidTarget");
            return;
        }

        const auto now = snap.timestampMs;
        if (now - lastSelectTime_ >= config_.selectCooldownMs) {
            lastSelectTime_ = now;
            syncWeaponLoadout(snap, *target, now);
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const double combatRange = adaptiveCombatRange(target, snap);
        const double repositionThreshold =
            std::max(static_cast<double>(config_.attackRange), combatRange) + rangeSlack(combatRange);
        const double dist = heroPos.distanceTo(Position(target->x, target->y));
        if (dist > repositionThreshold) {
            state_ = CombatState::Approaching;
            publishTelemetry(snap, target, "Reposition");
            return;
        }

        if (snap.hero.selectedTarget != currentTargetId_) {
            state_ = CombatState::Approaching;
            publishTelemetry(snap, target, "Relock");
            return;
        }

        if (now - lastAttackTime_ >= config_.attackCooldownMs) {
            lastAttackTime_ = now;

            engine_->attack();
            killStartTime_ = now;
            std::cout << "[Combat] Attacking " << target->name << "\n";
        }

        handleCombatMovement(snap, target);
        state_ = CombatState::AwaitingKill;
        publishTelemetry(snap, target, "AttackStart");
    }
    
    void handleAwaitingKill(const GameSnapshot& snap) {
        const ShipInfo* target = getCurrentTarget(snap);
        if (!target) {
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Cooldown;
            std::cout << "[Combat] Target eliminated\n";
            publishTelemetry(snap, nullptr, "TargetEliminated");
            return;
        }

        const NpcTargetConfig* targetConfig = matchTargetConfig(target->name);
        if (!targetConfig || !isCurrentTargetValid(snap, *target, *targetConfig)) {
            applyTargetLockout(currentTargetId_, snap.timestampMs, "invalid_target", 7000);
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "InvalidTarget");
            return;
        }

        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);
        const double combatRange = adaptiveCombatRange(target, snap);
        const double sustainedAttackEnvelope =
            std::max(static_cast<double>(config_.attackRange), combatRange) + rangeSlack(combatRange);
        const double dist = heroPos.distanceTo(Position(target->x, target->y));

        observeTargetDurability(snap, *target);

        if (now - lastSelectTime_ >= config_.selectCooldownMs) {
            lastSelectTime_ = now;
            syncWeaponLoadout(snap, *target, now);
        }

        if (snap.hero.selectedTarget != currentTargetId_) {
            state_ = CombatState::Approaching;
            publishTelemetry(snap, target, "Relock");
            return;
        }

        if (!snap.hero.isAttacking) {
            state_ = CombatState::Attacking;
            publishTelemetry(snap, target, "Reattack");
            return;
        }

        if (dist <= sustainedAttackEnvelope &&
            now - lastDamageProgressAtMs_ > DAMAGE_STALL_TIMEOUT_MS) {
            applyTargetLockout(currentTargetId_, now, "damage_stall");
            stopCombatActions(true, true);
            clearCurrentTargetState();
            state_ = CombatState::Searching;
            publishTelemetry(snap, nullptr, "DamageStall");
            return;
        }

        handleCombatMovement(snap, target);
    }
    
    void handleCooldown(const GameSnapshot& snap) {
        state_ = CombatState::Searching;
        clearCurrentTargetState();
        publishTelemetry(snap, nullptr, "Cooldown");
    }
    
    void handleCombatMovement(const GameSnapshot& snap, const ShipInfo* target) {
        auto now = snap.timestampMs;

        if (now - lastMoveTime_ < config_.moveCooldownMs) {
            return;
        }

        const double combatRange = adaptiveCombatRange(target, snap);
        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position targetPos(target->x, target->y);
        const double currentDistance = heroPos.distanceTo(targetPos);
        const double heroSpeed = static_cast<double>(std::max(snap.hero.speed, 250));
        const double npcSpeed = target->observedSpeedEma > 10.0f
            ? std::clamp(static_cast<double>(target->observedSpeedEma), 90.0, 520.0)
            : (target->speed > 0
                ? std::clamp(static_cast<double>(target->speed), 90.0, 520.0)
                : 0.0);
        const CombatTargetMotion motion = buildTargetMotion(heroPos, *target, heroSpeed, combatRange);
        const double anchorDistance = heroPos.distanceTo(motion.anchor);
        const auto combatCollect = findCombatCollectBox(snap, *target, combatRange);

        if (maybeHandleStuckRecovery(snap, target, currentDistance, 2600, "combat_movement_stalled")) {
            return;
        }

        const double radialSlack = rangeSlack(combatRange);
        if (anchorDistance > combatRange + radialSlack) {
            lastMoveTime_ = now;
            const Position moveTarget = navigation_.approachPosition(heroPos, motion.anchor, combatRange);
            moveWithinBounds(snap, moveTarget, MoveIntent::Pursuit);
            if (combatCollect.has_value()) {
                tryCollectCombatBox(snap, *combatCollect);
            }
            publishTelemetry(snap, target, "Pursuit");
            return;
        }

        if (anchorDistance < combatRange - radialSlack) {
            lastMoveTime_ = now;
            const Position createSpace = selectCreateSpacePosition(
                heroPos,
                motion.anchor,
                combatRange,
                orbitClockwise_
            );
            moveWithinBounds(snap, createSpace, MoveIntent::Escape);
            if (combatCollect.has_value()) {
                tryCollectCombatBox(snap, *combatCollect);
            }
            publishTelemetry(snap, target, "CreateSpace");
            return;
        }

        lastMoveTime_ = now;
        const OrbitSolution orbitSolution = selectOrbitSolution(
            snap,
            *target,
            combatRange,
            heroSpeed,
            npcSpeed,
            orbitClockwise_,
            combatCollect.has_value() ? combatCollect->box : nullptr
        );
        adoptOrbitDirection(orbitSolution, now);

        const Position orbitPos = orbitSolution.valid
            ? orbitSolution.point
            : navigation_.approachPosition(heroPos, motion.anchor, combatRange);
        moveWithinBounds(snap, orbitPos, MoveIntent::Orbit);
        if (combatCollect.has_value()) {
            tryCollectCombatBox(snap, *combatCollect);
        }

        if (combatCollect.has_value() && combatCollect->box) {
            publishTelemetry(snap, target, "Orbit+Collect");
        } else {
            publishTelemetry(snap, target, "Orbit");
        }
    }
    
    void handleAntibanMovement(const GameSnapshot& snap) {
        auto now = snap.timestampMs;
        
        if (now - lastAntibanMoveTime_ >= config_.antibanMoveIntervalMs) {
            lastAntibanMoveTime_ = now;
            
            Position heroPos(snap.hero.x, snap.hero.y);
            Position jittered = navigation_.jitter(heroPos, 100);
            
            const MapBounds bounds = currentBounds();
            if (bounds.contains(jittered, 200)) {
                moveWithinBounds(snap, jittered, MoveIntent::Antiban);
            }
            publishTelemetry(snap, getCurrentTarget(snap), "AntibanJitter");
        }
    }

    void syncWeaponLoadout(const GameSnapshot& snap, const ShipInfo& target, int64_t now) {
        engine_->switchAmmo(currentAmmoType_);

        const Position heroPos(snap.hero.x, snap.hero.y);
        const double targetDistance = heroPos.distanceTo(Position(target.x, target.y));
        if (snap.hero.selectedTarget != currentTargetId_ &&
            targetDistance <= targetSelectionRange()) {
            engine_->lockTarget(currentTargetId_);
        }

        const bool wantsAutoRocket = config_.useRockets && currentRocketType_ > 0;
        if (!wantsAutoRocket) {
            disableAutoRocket(now);
            return;
        }

        if (now - lastRocketSwitchTime_ >= config_.rocketCooldownMs) {
            engine_->switchRocket(currentRocketType_);
            lastRocketSwitchTime_ = now;
        }

        enableAutoRocket(now);
    }

    void enableAutoRocket(int64_t now) {
        if (autoRocketStateKnown_ && autoRocketEnabled_) {
            return;
        }
        if (now > 0 && now - lastAutoRocketToggleTime_ < AUTO_ROCKET_TOGGLE_COOLDOWN_MS) {
            return;
        }

        engine_->setAutoRocketEnabled(true);
        autoRocketEnabled_ = true;
        autoRocketStateKnown_ = true;
        lastAutoRocketToggleTime_ = now;
    }

    void disableAutoRocket(int64_t now, bool force = false) {
        if (!force && autoRocketStateKnown_ && !autoRocketEnabled_) {
            return;
        }
        if (!force && now > 0 &&
            now - lastAutoRocketToggleTime_ < AUTO_ROCKET_TOGGLE_COOLDOWN_MS) {
            return;
        }

        engine_->setAutoRocketEnabled(false);
        autoRocketEnabled_ = false;
        autoRocketStateKnown_ = true;
        lastAutoRocketToggleTime_ = now;
    }
