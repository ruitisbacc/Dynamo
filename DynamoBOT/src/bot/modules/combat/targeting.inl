    [[nodiscard]] static double combinedDurabilityPercent(const ShipInfo& target) {
        const int32_t maxDurability =
            std::max(target.maxHealth, 0) + std::max(target.maxShield, 0);
        if (maxDurability <= 0) {
            return 100.0;
        }

        const int32_t currentDurability =
            std::max(target.health, 0) + std::max(target.shield, 0);
        return static_cast<double>(currentDurability) * 100.0 /
               static_cast<double>(maxDurability);
    }

    [[nodiscard]] bool allowsOwnedTarget(const NpcTargetConfig& config) const {
        return config_.targetEngagedNpc || config.ignoreOwnership;
    }

    [[nodiscard]] bool isLowHpFollowActive(const ShipInfo& target,
                                           const NpcTargetConfig& config) const {
        return config.followOnLowHp &&
               combinedDurabilityPercent(target) <=
                   static_cast<double>(std::clamp(config.followOnLowHpPercent, 1, 100));
    }

    [[nodiscard]] double configuredCombatRange(const NpcTargetConfig& config) const {
        return std::clamp(static_cast<double>(config.range), 200.0, 800.0);
    }

    [[nodiscard]] bool isCombatCollectEnabled(const GameSnapshot& snap) const {
        return collectConfig_.enabled &&
               collectConfig_.collectDuringCombat &&
               snap.mode == BotMode::KillCollect;
    }

    [[nodiscard]] bool shouldCollectCombatBox(const BoxInfo& box, const GameSnapshot& snap) const {
        for (const auto& targetBox : collectConfig_.targetBoxes) {
            if (!targetBox.enabled || targetBox.type != box.type) {
                continue;
            }

            if (targetBox.type == static_cast<int32_t>(BoxType::GreenBox) &&
                collectConfig_.skipBootyIfNoKeys &&
                snap.hero.bootyKeys <= 0) {
                return false;
            }

            if (targetBox.type == static_cast<int32_t>(BoxType::CargoBox) &&
                collectConfig_.skipResourceIfCargoFull &&
                snap.hero.isCargoFull()) {
                return false;
            }

            return true;
        }
        return false;
    }

    [[nodiscard]] int32_t combatCollectBoxPriority(int32_t boxType) const {
        for (const auto& targetBox : collectConfig_.targetBoxes) {
            if (targetBox.enabled && targetBox.type == boxType) {
                return targetBox.priority;
            }
        }
        return 0;
    }

    [[nodiscard]] int32_t highestCombatCollectPriority() const {
        int32_t best = 0;
        for (const auto& targetBox : collectConfig_.targetBoxes) {
            if (targetBox.enabled) {
                best = std::max(best, targetBox.priority);
            }
        }
        return best;
    }

    [[nodiscard]] double targetDurabilityRatio(const ShipInfo& target) const {
        const int64_t maxDurability =
            static_cast<int64_t>(std::max(target.maxHealth, 0)) +
            static_cast<int64_t>(std::max(target.maxShield, 0));
        const int64_t currentDurability =
            static_cast<int64_t>(std::max(target.health, 0)) +
            static_cast<int64_t>(std::max(target.shield, 0));

        if (maxDurability <= 0) {
            return 1.0;
        }

        return std::clamp(
            static_cast<double>(currentDurability) / static_cast<double>(maxDurability),
            0.0,
            1.0
        );
    }

    [[nodiscard]] double dynamicCombatCollectDistance(const ShipInfo& target) const {
        const double configuredMaxDistance = std::max(
            static_cast<double>(collectConfig_.combatCollectMaxDistance),
            0.0
        );
        const double maxDistance = std::clamp(
            configuredMaxDistance + 100.0,
            0.0,
            950.0
        );
        if (maxDistance <= 0.0) {
            return 0.0;
        }

        const double minDistance = std::min(320.0, maxDistance);
        return minDistance + (maxDistance - minDistance) * targetDurabilityRatio(target);
    }

    [[nodiscard]] static double combatCollectInwardLimit(double combatRange) {
        return combatRange - std::max(COMBAT_COLLECT_INWARD_BUFFER, combatRange * 0.10);
    }

    [[nodiscard]] std::optional<CombatCollectSelection>
    findCombatCollectBox(const GameSnapshot& snap,
                         const ShipInfo& target,
                         double combatRange) const {
        if (!isCombatCollectEnabled(snap)) {
            return std::nullopt;
        }

        const int32_t highestPriority = highestCombatCollectPriority();
        if (highestPriority <= 0) {
            return std::nullopt;
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position targetPos(target.x, target.y);
        const double heroToTargetDistance = heroPos.distanceTo(targetPos);
        const double dynamicMaxDistance = dynamicCombatCollectDistance(target);
        if (dynamicMaxDistance <= 0.0) {
            return std::nullopt;
        }

        const BoxInfo* bestBox = nullptr;
        double bestScore = -std::numeric_limits<double>::infinity();

        for (const auto& box : snap.entities.boxes) {
            if (!box.existsOnMap || !shouldCollectCombatBox(box, snap)) {
                continue;
            }

            const int32_t priority = combatCollectBoxPriority(box.type);
            if (priority < highestPriority) {
                continue;
            }

            const Position approachPos = collectApproachPosition(box);
            const double heroToBoxDistance = heroPos.distanceTo(approachPos);
            if (heroToBoxDistance > dynamicMaxDistance) {
                continue;
            }

            if (heroToTargetDistance > 0.0 && heroToBoxDistance >= heroToTargetDistance + 180.0) {
                continue;
            }

            const double targetToBoxDistance = targetPos.distanceTo(approachPos);
            if (targetToBoxDistance < combatCollectInwardLimit(combatRange)) {
                continue;
            }

            const double rangePenalty = std::abs(targetToBoxDistance - combatRange);
            const double score =
                priority * 1400.0 -
                heroToBoxDistance * 1.00 -
                rangePenalty * 0.70 +
                (targetToBoxDistance + 20.0 >= heroToTargetDistance ? 140.0 : 0.0);

            if (score > bestScore) {
                bestScore = score;
                bestBox = &box;
            }
        }

        if (!bestBox) {
            return std::nullopt;
        }

        CombatCollectSelection selection;
        selection.box = bestBox;
        selection.approachPosition = collectApproachPosition(*bestBox);
        return selection;
    }

    [[nodiscard]] std::optional<CombatCollectSelection>
    findApproachCollectBox(const GameSnapshot& snap,
                           const ShipInfo& target,
                           const Position& plannedMoveTarget,
                           double /*combatRange*/) const {
        if (!isCombatCollectEnabled(snap)) {
            return std::nullopt;
        }

        const int32_t highestPriority = highestCombatCollectPriority();
        if (highestPriority <= 0) {
            return std::nullopt;
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position targetPos(target.x, target.y);
        const double heroToTargetDistance = heroPos.distanceTo(targetPos);
        const double directPathDistance = heroPos.distanceTo(plannedMoveTarget);
        if (directPathDistance <= 40.0) {
            return std::nullopt;
        }

        const double dynamicMaxDistance = dynamicCombatCollectDistance(target);
        if (dynamicMaxDistance <= 0.0) {
            return std::nullopt;
        }

        const double corridorWidth = std::clamp(
            directPathDistance * 0.16,
            APPROACH_COLLECT_CORRIDOR_MIN,
            APPROACH_COLLECT_CORRIDOR_MAX
        );
        const double plannedTargetDistance = plannedMoveTarget.distanceTo(targetPos);

        const BoxInfo* bestBox = nullptr;
        double bestScore = -std::numeric_limits<double>::infinity();

        for (const auto& box : snap.entities.boxes) {
            if (!box.existsOnMap || !shouldCollectCombatBox(box, snap)) {
                continue;
            }

            const int32_t priority = combatCollectBoxPriority(box.type);
            if (priority < highestPriority) {
                continue;
            }

            const Position approachPos = collectApproachPosition(box);
            const double heroToBoxDistance = heroPos.distanceTo(approachPos);
            if (heroToBoxDistance > dynamicMaxDistance + 100.0) {
                continue;
            }

            if (heroToTargetDistance > 0.0 &&
                heroToBoxDistance >= heroToTargetDistance + 140.0) {
                continue;
            }

            const double deviation =
                distanceToSegment(approachPos, heroPos, plannedMoveTarget);
            if (deviation > corridorWidth) {
                continue;
            }

            const double detour =
                heroToBoxDistance +
                approachPos.distanceTo(plannedMoveTarget) -
                directPathDistance;
            if (detour > APPROACH_COLLECT_MAX_DETOUR) {
                continue;
            }

            const double targetToBoxDistance = targetPos.distanceTo(approachPos);
            if (targetToBoxDistance + 50.0 < plannedTargetDistance) {
                continue;
            }

            const double score =
                priority * 1700.0 -
                heroToBoxDistance * 0.70 -
                deviation * 3.60 -
                detour * 3.00 -
                std::abs(targetToBoxDistance - plannedTargetDistance) * 0.90 +
                (targetToBoxDistance + 15.0 >= plannedTargetDistance ? 120.0 : 0.0);

            if (score > bestScore) {
                bestScore = score;
                bestBox = &box;
            }
        }

        if (!bestBox) {
            return std::nullopt;
        }

        CombatCollectSelection selection;
        selection.box = bestBox;
        selection.approachPosition = collectApproachPosition(*bestBox);
        return selection;
    }

    void tryCollectCombatBox(const GameSnapshot& snap,
                             const CombatCollectSelection& selection) {
        if (!selection.box || !selection.box->existsOnMap) {
            return;
        }

        if (snap.timestampMs - lastCombatCollectTime_ < collectConfig_.collectCooldownMs) {
            return;
        }
        if (snap.timestampMs < combatCollectBlockedUntilMs_) {
            return;
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position boxPos(selection.box->x, selection.box->y);
        const double actualDistance = heroPos.distanceTo(boxPos);
        const double approachDistance = heroPos.distanceTo(selection.approachPosition);
        if (std::min(actualDistance, approachDistance) > COMBAT_COLLECT_RANGE) {
            return;
        }

        lastCombatCollectTime_ = snap.timestampMs;
        combatCollectBlockedUntilMs_ =
            snap.timestampMs + collectPostAttemptWaitMs(*selection.box);
        engine_->collect(selection.box->id);
    }

    [[nodiscard]] double adaptiveCombatRange(const ShipInfo* target,
                                             const GameSnapshot&) const {
        if (target) {
            if (const auto* config = matchTargetConfig(target->name)) {
                const double configuredRange = configuredCombatRange(*config);
                return isLowHpFollowActive(*target, *config)
                    ? configuredRange * LOW_HP_FOLLOW_RANGE_SCALE
                    : configuredRange;
            }
        }

        return idealCombatRange();
    }

    void adoptOrbitDirection(const OrbitSolution& solution, int64_t now) {
        if (!solution.valid || solution.clockwise == orbitClockwise_) {
            return;
        }

        orbitClockwise_ = solution.clockwise;
        lastOrbitDirectionChangeAtMs_ = now;
    }

    void publishTelemetry(const GameSnapshot& snap,
                          const ShipInfo* target,
                          std::string_view decision) {
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = state_;
        telemetry_.movementMode = currentMovementMode_;
        telemetry_.movementDecision = std::string(decision);
        telemetry_.targetId = target ? target->id : 0;
        telemetry_.targetName = target ? target->name : std::string{};
        telemetry_.targetDistance = target
            ? static_cast<float>(Position(snap.hero.x, snap.hero.y).distanceTo(Position(target->x, target->y)))
            : 0.0f;
        telemetry_.stuckRecoveryActive = recoveryUntilMs_ > snap.timestampMs;
        telemetry_.lastProgressMs = lastProgressTime_;
        telemetry_.lastRecoveryMs = lastRecoveryAtMs_;
    }

    void resetTracking(const GameSnapshot& snap, const ShipInfo* target) {
        trackedTargetId_ = target ? target->id : 0;
        lastTrackedHeroPos_ = Position(snap.hero.x, snap.hero.y);
        lastTrackedTargetDistance_ = target
            ? lastTrackedHeroPos_.distanceTo(Position(target->x, target->y))
            : std::numeric_limits<double>::max();
        currentTargetAcquiredAtMs_ = snap.timestampMs;
        lastProgressTime_ = snap.timestampMs;
        lastDamageProgressAtMs_ = snap.timestampMs;
        currentTargetDurability_ = target
            ? std::max(target->health, 0) + std::max(target->shield, 0)
            : 0;
        currentTargetRecoveries_ = 0;
        recoveryUntilMs_ = 0;

        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.targetId = target ? target->id : 0;
        telemetry_.targetName = target ? target->name : std::string{};
        telemetry_.lastProgressMs = lastProgressTime_;
    }

    void updateProgressSample(const GameSnapshot& snap, double targetDistance) {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const double heroDelta = heroPos.distanceTo(lastTrackedHeroPos_);
        const double distanceImprovement = lastTrackedTargetDistance_ - targetDistance;
        const double minHeroDelta = std::clamp(
            static_cast<double>(std::max(snap.hero.speed, 250)) * 0.12,
            30.0,
            80.0
        );

        if (heroDelta >= minHeroDelta || distanceImprovement >= 45.0) {
            lastProgressTime_ = snap.timestampMs;
        }

        lastTrackedHeroPos_ = heroPos;
        lastTrackedTargetDistance_ = targetDistance;
    }

    void triggerStuckRecovery(const GameSnapshot& snap,
                              const ShipInfo* target,
                              std::string_view reason) {
        if (!target) {
            return;
        }

        if (movement_) {
            movement_->release(name());
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const MapBounds bounds = currentBounds();
        const double recoveryRange = std::max(160.0, adaptiveCombatRange(target, snap));
        const double heroSpeed = static_cast<double>(std::max(snap.hero.speed, 250));
        const double npcSpeed = target->observedSpeedEma > 10.0f
            ? std::clamp(static_cast<double>(target->observedSpeedEma), 90.0, 520.0)
            : (target->speed > 0
                ? std::clamp(static_cast<double>(target->speed), 90.0, 520.0)
                : 0.0);
        const OrbitSolution recoveryOrbit = selectOrbitSolution(
            snap,
            *target,
            recoveryRange,
            heroSpeed,
            npcSpeed,
            !orbitClockwise_
        );
        const Position recoveryFallback = navigation_.orbitPosition(
            heroPos,
            Position(target->x, target->y),
            recoveryRange,
            !orbitClockwise_
        );
        recoveryMoveTarget_ = recoveryOrbit.valid ? recoveryOrbit.point : recoveryFallback;
        recoveryMoveTarget_ = bounds.clamp(recoveryMoveTarget_, 220.0);

        moveWithinBounds(snap, recoveryMoveTarget_, MoveIntent::Escape);

        orbitClockwise_ = recoveryOrbit.valid ? recoveryOrbit.clockwise : !orbitClockwise_;
        lastOrbitDirectionChangeAtMs_ = snap.timestampMs;
        ++currentTargetRecoveries_;
        recoveryUntilMs_ = snap.timestampMs + 900;
        lastRecoveryAtMs_ = snap.timestampMs;
        lastProgressTime_ = snap.timestampMs;
        lastTrackedHeroPos_ = heroPos;
        lastTrackedTargetDistance_ = heroPos.distanceTo(Position(target->x, target->y));

        {
            std::lock_guard<std::mutex> lock(telemetryMutex_);
            telemetry_.stuckRecoveries++;
            telemetry_.recoveryReason = std::string(reason);
            telemetry_.lastRecoveryMs = lastRecoveryAtMs_;
            telemetry_.stuckRecoveryActive = true;
        }

        std::cout << "[Combat] Stuck recovery: " << reason
                  << " -> (" << recoveryMoveTarget_.x << ", " << recoveryMoveTarget_.y << ")\n";
    }

    [[nodiscard]] bool maybeHandleStuckRecovery(const GameSnapshot& snap,
                                                const ShipInfo* target,
                                                double targetDistance,
                                                int64_t timeoutMs,
                                                std::string_view reason) {
        if (!target) {
            return false;
        }

        if (trackedTargetId_ != target->id) {
            resetTracking(snap, target);
            return false;
        }

        if (recoveryUntilMs_ > snap.timestampMs) {
            publishTelemetry(snap, target, "Recovery");
            return true;
        }

        updateProgressSample(snap, targetDistance);
        if (snap.timestampMs - lastProgressTime_ < timeoutMs) {
            return false;
        }

        triggerStuckRecovery(snap, target, reason);
        publishTelemetry(snap, target, "Recovery");
        return true;
    }
    void observeTargetDurability(const GameSnapshot& snap, const ShipInfo& target) {
        const int32_t durability = std::max(target.health, 0) + std::max(target.shield, 0);
        if (currentTargetDurability_ == 0 || durability < currentTargetDurability_) {
            lastDamageProgressAtMs_ = snap.timestampMs;
        }
        currentTargetDurability_ = durability;
    }

    [[nodiscard]] bool isCurrentTargetValid(const GameSnapshot& snap,
                                            const ShipInfo& target,
                                            const NpcTargetConfig& config) const {
        if (target.isDestroyed) {
            return false;
        }
        if (isTargetLockedOut(target.id, snap.timestampMs)) {
            return false;
        }
        if (!allowsOwnedTarget(config) && isNpcEngagedByOtherShip(snap, target)) {
            return false;
        }

        const double maxDistance = effectiveMaxDistance(config);
        if (maxDistance <= 0.0 || isLowHpFollowActive(target, config)) {
            return true;
        }

        const Position heroPos(snap.hero.x, snap.hero.y);
        const double dist = heroPos.distanceTo(Position(target.x, target.y));
        return dist <= maxDistance + TARGET_LEASH_DISTANCE;
    }

    [[nodiscard]] bool isNpcEngagedByOtherShip(const GameSnapshot& snap,
                                               const ShipInfo& npc) const {
        for (const auto& ship : snap.entities.ships) {
            if (ship.id == 0 || ship.id == snap.hero.id || ship.id == npc.id) {
                continue;
            }
            if (ship.isDestroyed || ship.isCloaked || ship.isNpc) {
                continue;
            }
            if (ship.selectedTarget == npc.id && ship.isAttacking) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<CombatTargetSelection>
    findBestTarget(const GameSnapshot& snap) {
        std::optional<CombatTargetSelection> best;
        const Position heroPos(snap.hero.x, snap.hero.y);

        for (const auto& ship : snap.entities.ships) {
            if (ship.isDestroyed || ship.isCloaked || ship.id == snap.hero.id) {
                continue;
            }

            const NpcTargetConfig* matchedConfig = matchTargetConfig(ship.name);
            if (!matchedConfig || !shouldTreatAsConfiguredNpc(ship, snap)) {
                continue;
            }
            if (!allowsOwnedTarget(*matchedConfig) &&
                isNpcEngagedByOtherShip(snap, ship)) {
                continue;
            }

            const double dist = heroPos.distanceTo(Position(ship.x, ship.y));
            const double maxDistance = effectiveMaxDistance(*matchedConfig);
            if (maxDistance > 0.0 && dist > maxDistance) {
                continue;
            }

            if (isTargetLockedOut(ship.id, snap.timestampMs)) {
                continue;
            }

            const double durabilityRatio = targetDurabilityRatio(ship);
            double score = matchedConfig->priority * 10000.0 - dist;
            if (snap.hero.selectedTarget == ship.id) {
                score += 1100.0;
            }
            if (ship.selectedTarget == snap.hero.id && ship.isAttacking) {
                score += 2600.0;
            }
            score += (1.0 - durabilityRatio) * 650.0;

            const CombatTargetSelection candidate{&ship, matchedConfig, dist, durabilityRatio, score};
            if (!best.has_value() ||
                candidate.score > best->score + 1.0 ||
                (std::abs(candidate.score - best->score) <= 1.0 &&
                 (candidate.distance + 25.0 < best->distance ||
                  (std::abs(candidate.distance - best->distance) <= 25.0 &&
                   (candidate.durabilityRatio + 0.03 < best->durabilityRatio ||
                    (std::abs(candidate.durabilityRatio - best->durabilityRatio) <= 0.03 &&
                     candidate.ship->id < best->ship->id)))))) {
                best = candidate;
            }
        }

        return best;
    }
    
    [[nodiscard]] const NpcTargetConfig* matchTargetConfig(const std::string& name) const {
        for (std::size_t i = 0; i < config_.targets.size() && i < targetPatterns_.size(); ++i) {
            if (equalsIgnoreCase(name, config_.targets[i].namePattern)) {
                return &config_.targets[i];
            }
            if (std::regex_search(name, targetPatterns_[i])) {
                return &config_.targets[i];
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool shouldTreatAsConfiguredNpc(const ShipInfo& ship,
                                                  const GameSnapshot& snap) const {
        if (ship.id == 0 || ship.id == snap.hero.id || ship.name.empty()) {
            return false;
        }

        if (ship.isNpc) {
            return true;
        }

        if (!isKnownNpc(ship.name)) {
            return false;
        }

        if (snap.mapName.empty()) {
            return true;
        }

        return npcSpawnsOnMap(ship.name, snap.mapName);
    }
    
    [[nodiscard]] const ShipInfo* getCurrentTarget(const GameSnapshot& snap) const {
        if (currentTargetId_ == 0) {
            return nullptr;
        }

        for (const auto& ship : snap.entities.ships) {
            if (ship.id == currentTargetId_ && shouldTreatAsConfiguredNpc(ship, snap)) {
                return &ship;
            }
        }
        return nullptr;
    }
