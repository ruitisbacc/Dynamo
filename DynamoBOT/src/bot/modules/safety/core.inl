    void transitionTo(SafetyState newState, int64_t nowMs) {
        if (state_ == newState) {
            return;
        }
        state_ = newState;
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = newState;
        telemetry_.lastStateChangeMs = nowMs;
    }

    void updateThreats(const GameSnapshot& snap) {
        for (const auto& ship : snap.entities.enemies) {
            const bool attackingUs = ship.selectedTarget == snap.hero.id && ship.isAttacking;
            threatTracker_.updateEnemy(EnemyObservation{
                .playerId = ship.id,
                .username = ship.name,
                .x = ship.x,
                .y = ship.y,
                .targetX = ship.targetX,
                .targetY = ship.targetY,
                .isMoving = ship.isMoving,
                .droneCount = ship.droneCount,
                .isAttackingUs = attackingUs,
                .timestampMs = snap.timestampMs,
            });

            if (attackingUs && sessionState_) {
                sessionState_->rememberedAggressorIds.insert(ship.id);
            }
        }

        threatTracker_.cleanupOldEnemies(snap.timestampMs, config_.enemySeenTimeoutMs);
    }

    [[nodiscard]] double calculateHpPercent(const GameSnapshot& snap) const {
        if (snap.hero.maxHealth <= 0) return 100.0;
        return (static_cast<double>(snap.hero.health) / snap.hero.maxHealth) * 100.0;
    }

    [[nodiscard]] static bool isRepairConfigId(int32_t configId) {
        return configId == 1 || configId == 2;
    }

    [[nodiscard]] static size_t repairConfigIndex(int32_t configId) {
        return configId == 2 ? 1u : 0u;
    }

    void resetRepairShieldTracking() {
        for (auto& state : repairConfigShieldStates_) {
            state = RepairConfigShieldObservation{};
        }
    }

    void observeRepairShieldState(const GameSnapshot& snap) {
        if (!isRepairConfigId(snap.hero.activeConfig)) {
            return;
        }

        auto& state = repairConfigShieldStates_[repairConfigIndex(snap.hero.activeConfig)];
        state.observed = true;
        state.shield = std::max(snap.hero.shield, 0);
        state.maxShield = std::max(snap.hero.maxShield, 0);
    }

    [[nodiscard]] const RepairConfigShieldObservation* repairShieldObservationFor(int32_t configId) const {
        if (!isRepairConfigId(configId)) {
            return nullptr;
        }
        return &repairConfigShieldStates_[repairConfigIndex(configId)];
    }

    [[nodiscard]] bool isRepairShieldComplete(int32_t configId) const {
        const auto* state = repairShieldObservationFor(configId);
        if (state == nullptr || !state->observed) {
            return false;
        }
        if (state->maxShield <= 0) {
            return true;
        }
        return state->shield >= state->maxShield;
    }

    [[nodiscard]] std::optional<int32_t> nextRepairShieldConfig(int32_t activeConfigId) const {
        const int32_t currentConfigId = isRepairConfigId(activeConfigId) ? activeConfigId : 1;
        const int32_t otherConfigId = currentConfigId == 1 ? 2 : 1;

        if (!isRepairShieldComplete(currentConfigId)) {
            return currentConfigId;
        }

        const auto* otherState = repairShieldObservationFor(otherConfigId);
        if (otherState == nullptr || !otherState->observed || !isRepairShieldComplete(otherConfigId)) {
            return otherConfigId;
        }

        return std::nullopt;
    }

    [[nodiscard]] bool areAllRepairShieldsComplete() const {
        return isRepairShieldComplete(1) && isRepairShieldComplete(2);
    }

    [[nodiscard]] static double distanceToSegment(const Position& point,
                                                  const Position& segmentStart,
                                                  const Position& segmentEnd) {
        const double segmentDx = segmentEnd.x - segmentStart.x;
        const double segmentDy = segmentEnd.y - segmentStart.y;
        const double segmentLengthSq = segmentDx * segmentDx + segmentDy * segmentDy;
        if (segmentLengthSq <= 1e-6) {
            return point.distanceTo(segmentStart);
        }

        const double t = std::clamp(
            ((point.x - segmentStart.x) * segmentDx + (point.y - segmentStart.y) * segmentDy) /
                segmentLengthSq,
            0.0,
            1.0
        );
        const Position projection{
            segmentStart.x + segmentDx * t,
            segmentStart.y + segmentDy * t
        };
        return point.distanceTo(projection);
    }

    [[nodiscard]] static double normalizedDot(const Position& lhs, const Position& rhs) {
        const double lhsLength = std::sqrt(lhs.x * lhs.x + lhs.y * lhs.y);
        const double rhsLength = std::sqrt(rhs.x * rhs.x + rhs.y * rhs.y);
        if (lhsLength <= 1e-6 || rhsLength <= 1e-6) {
            return 0.0;
        }
        return ((lhs.x * rhs.x) + (lhs.y * rhs.y)) / (lhsLength * rhsLength);
    }

    [[nodiscard]] std::optional<EnemyInfo> findFreshEnemyById(int32_t playerId,
                                                              int64_t nowMs) const {
        return threatTracker_.getFreshEnemy(playerId, nowMs, config_.enemySeenTimeoutMs);
    }

    [[nodiscard]] std::vector<EnemyInfo> collectVisibleEnemies(const GameSnapshot& snap) const {
        std::vector<EnemyInfo> result;
        result.reserve(snap.entities.enemies.size());

        for (const auto& ship : snap.entities.enemies) {
            if (auto tracked = findFreshEnemyById(ship.id, snap.timestampMs); tracked.has_value()) {
                result.push_back(*tracked);
                continue;
            }

            const bool attackingUs = ship.selectedTarget == snap.hero.id && ship.isAttacking;
            result.push_back(EnemyInfo{
                .playerId = ship.id,
                .username = ship.name,
                .firstSeenTime = snap.timestampMs,
                .lastSeenTime = snap.timestampMs,
                .lastAggressionTime = attackingUs ? snap.timestampMs : 0,
                .sightings = 1,
                .droneCount = ship.droneCount,
                .isAttackingUs = attackingUs,
                .isAdmin =
                    ship.droneCount > adminConfig_.droneCountThreshold ||
                    detail::isKnownAdminName(ship.name),
                .isMoving = ship.isMoving,
                .lastX = ship.x,
                .lastY = ship.y,
                .lastTargetX = ship.targetX,
                .lastTargetY = ship.targetY,
            });
        }

        return result;
    }

    [[nodiscard]] std::optional<EnemyInfo> pickNearestEnemy(const std::vector<EnemyInfo>& enemies,
                                                            const Position& heroPos) const {
        if (enemies.empty()) {
            return std::nullopt;
        }

        const EnemyInfo* best = nullptr;
        double bestDistance = std::numeric_limits<double>::max();
        for (const auto& enemy : enemies) {
            const double distance = heroPos.distanceTo(Position(enemy.lastX, enemy.lastY));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = &enemy;
            }
        }

        return best ? std::optional<EnemyInfo>(*best) : std::nullopt;
    }

    [[nodiscard]] bool isActivelyAttackingNpc(const GameSnapshot& snap) const {
        if (!snap.hero.isAttacking || snap.hero.selectedTarget == 0) {
            return false;
        }

        return std::any_of(
            snap.entities.npcs.begin(),
            snap.entities.npcs.end(),
            [&snap](const ShipInfo& npc) { return npc.id == snap.hero.selectedTarget; }
        );
    }

    [[nodiscard]] bool shouldDelayRepairForNpcKill(const GameSnapshot& snap,
                                                   double hpPercent,
                                                   const EnemyFleeAssessment& assessment) const {
        if (hpPercent <= config_.minHpPercent || hpPercent >= config_.repairHpPercent) {
            return false;
        }

        if (assessment.active || assessment.adminOverride) {
            return false;
        }

        return isActivelyAttackingNpc(snap);
    }

    [[nodiscard]] EnemyFleeAssessment assessEnemyFlee(const GameSnapshot& snap,
                                                      const ThreatSummary& summary) const {
        EnemyFleeAssessment assessment;
        assessment.visibleEnemies = collectVisibleEnemies(snap);

        const Position heroPos(snap.hero.x, snap.hero.y);
        if (summary.primaryThreatId != 0) {
            for (const auto& enemy : assessment.visibleEnemies) {
                if (enemy.playerId == summary.primaryThreatId) {
                    assessment.primaryThreat = enemy;
                    break;
                }
            }
        }

        const auto chooseRememberedAggressor = [&]() -> std::optional<EnemyInfo> {
            const auto rememberedAggressorIds = sessionState_
                ? sessionState_->rememberedAggressorIds
                : std::unordered_set<int32_t>{};
            const EnemyInfo* best = nullptr;
            double bestDistance = std::numeric_limits<double>::max();
            bool bestIsCurrentAttacker = false;

            for (const auto& enemy : assessment.visibleEnemies) {
                if (!rememberedAggressorIds.contains(enemy.playerId)) {
                    continue;
                }

                const bool currentAttacker = enemy.isAttackingUs;
                const double distance = heroPos.distanceTo(Position(enemy.lastX, enemy.lastY));
                if (best == nullptr ||
                    (currentAttacker && !bestIsCurrentAttacker) ||
                    (currentAttacker == bestIsCurrentAttacker && distance < bestDistance)) {
                    best = &enemy;
                    bestDistance = distance;
                    bestIsCurrentAttacker = currentAttacker;
                }
            }

            if (best != nullptr) {
                assessment.visibleRememberedAggressor = true;
                return *best;
            }
            return std::nullopt;
        };

        if (summary.adminNearby) {
            assessment.adminOverride = true;
            assessment.active = true;
            if (!assessment.primaryThreat.has_value()) {
                assessment.primaryThreat = pickNearestEnemy(assessment.visibleEnemies, heroPos);
            }
            return assessment;
        }

        switch (config_.fleeMode) {
            case SafetyFleeMode::OnEnemySeen:
                assessment.active = !assessment.visibleEnemies.empty();
                if (assessment.active && !assessment.primaryThreat.has_value()) {
                    assessment.primaryThreat = pickNearestEnemy(assessment.visibleEnemies, heroPos);
                }
                break;

            case SafetyFleeMode::OnAttack:
                assessment.primaryThreat = chooseRememberedAggressor();
                assessment.active = assessment.primaryThreat.has_value();
                break;

            case SafetyFleeMode::None:
            default:
                break;
        }

        return assessment;
    }

    [[nodiscard]] double scoreEscapeAnchor(const Position& heroPos,
                                           const RepairAnchor& anchor,
                                           const EnemyFleeAssessment& assessment) const {
        double score = heroPos.distanceTo(anchor.position);

        if (anchor.kind == RepairAnchorKind::Station) {
            score -= 120.0;
        }

        if (anchor.kind == RepairAnchorKind::Portal) {
            if (isNeverJumpPortalTarget(anchor.label)) {
                score += 9000.0;
            } else if (isOwnSafePortalTarget(anchor.label)) {
                score -= 260.0;
            } else if (isEnemyFactionPortalTarget(anchor.label)) {
                if (shouldJumpEnemyFactionPortal(heroPos, assessment)) {
                    score -= 140.0;
                } else {
                    score += 4200.0;
                }
            } else {
                score += 2600.0;
            }
        }

        if (!assessment.primaryThreat.has_value()) {
            return score;
        }

        const Position primaryThreatPos(
            assessment.primaryThreat->lastX,
            assessment.primaryThreat->lastY
        );
        const double heroThreatDistance = heroPos.distanceTo(primaryThreatPos);
        const double anchorThreatDistance = anchor.position.distanceTo(primaryThreatPos);
        score += std::max(0.0, heroThreatDistance + 180.0 - anchorThreatDistance) * 3.0;
        score -= std::max(0.0, anchorThreatDistance - heroThreatDistance) * 0.45;

        const Position awayVector(heroPos.x - primaryThreatPos.x, heroPos.y - primaryThreatPos.y);
        const Position travelVector(anchor.position.x - heroPos.x, anchor.position.y - heroPos.y);
        score -= normalizedDot(travelVector, awayVector) * 520.0;

        for (const auto& enemy : assessment.visibleEnemies) {
            const Position enemyPos(enemy.lastX, enemy.lastY);
            const double heroEnemyDistance = heroPos.distanceTo(enemyPos);
            const double anchorEnemyDistance = anchor.position.distanceTo(enemyPos);
            const double corridorDistance = distanceToSegment(enemyPos, heroPos, anchor.position);

            if (anchorEnemyDistance < heroEnemyDistance) {
                score += (heroEnemyDistance - anchorEnemyDistance) * 1.6;
            } else {
                score -= std::min(anchorEnemyDistance - heroEnemyDistance, 600.0) * 0.18;
            }

            if (anchorEnemyDistance < ESCAPE_ENEMY_PROXIMITY_RADIUS) {
                score += (ESCAPE_ENEMY_PROXIMITY_RADIUS - anchorEnemyDistance) * 2.4;
            }

            if (corridorDistance < ESCAPE_CORRIDOR_DANGER_RADIUS) {
                score += (ESCAPE_CORRIDOR_DANGER_RADIUS - corridorDistance) * 2.8;
            }

            if (enemy.playerId == assessment.primaryThreat->playerId) {
                score += std::max(0.0, heroThreatDistance + 320.0 - anchorThreatDistance) * 1.4;
            }
        }

        return score;
    }

    void publishTelemetry(const GameSnapshot& snap,
                          const ThreatSummary& summary,
                          std::string_view decision) {
        const auto assessment = assessEnemyFlee(snap, summary);
        const Position heroPos(snap.hero.x, snap.hero.y);
        int32_t visibleAttackers = 0;
        int32_t visibleCloseEnemies = 0;
        constexpr double kTelemetryCloseThreatRange = 2200.0;

        for (const auto& enemy : assessment.visibleEnemies) {
            if (enemy.isAttackingUs) {
                ++visibleAttackers;
            }
            if (heroPos.distanceTo(Position(enemy.lastX, enemy.lastY)) <= kTelemetryCloseThreatRange) {
                ++visibleCloseEnemies;
            }
        }

        int32_t primaryThreatId = summary.primaryThreatId;
        double primaryThreatDistance = summary.primaryThreatDistance;
        if (assessment.primaryThreat.has_value()) {
            primaryThreatId = assessment.primaryThreat->playerId;
            primaryThreatDistance = heroPos.distanceTo(
                Position(assessment.primaryThreat->lastX, assessment.primaryThreat->lastY)
            );
        }

        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = state_;
        telemetry_.threatLevel = summary.level;
        telemetry_.decision = std::string(decision);
        telemetry_.escapeMap = escapePortalMap_;
        telemetry_.escapePortalId = escapePortalId_;
        telemetry_.visibleEnemies = static_cast<int32_t>(assessment.visibleEnemies.size());
        telemetry_.closeEnemies = visibleCloseEnemies;
        telemetry_.attackers = visibleAttackers;
        telemetry_.primaryThreatId = primaryThreatId;
        telemetry_.hpPercent = static_cast<float>(calculateHpPercent(snap));
        telemetry_.primaryThreatDistance = static_cast<float>(primaryThreatDistance);
        telemetry_.fleeTargetX = static_cast<float>(fleeTarget_.x);
        telemetry_.fleeTargetY = static_cast<float>(fleeTarget_.y);
        telemetry_.beingAttacked = visibleAttackers > 0;
        telemetry_.adminSeenRecently =
            threatTracker_.adminSeenRecently(
                snap.timestampMs,
                std::max(config_.adminEscapeDelayMs, adminConfig_.escapeDelayMs)
            );
        telemetry_.hostileApproachDetected = summary.hostileApproachDetected;
        telemetry_.fleeRetargets = fleeRetargetCount_;
        telemetry_.lastProgressMs = lastProgressTime_;
    }

    [[nodiscard]] bool isAvoidedMap(const std::string& mapName) const {
        if (mapName.empty()) {
            return false;
        }
        return std::find(mapConfig_.avoidMaps.begin(), mapConfig_.avoidMaps.end(), mapName) !=
               mapConfig_.avoidMaps.end();
    }

    [[nodiscard]] static char factionPrefixFor(int32_t heroFraction) {
        switch (heroFraction) {
            case 1: return 'R';
            case 2: return 'E';
            case 3: return 'U';
            default: return '\0';
        }
    }

    [[nodiscard]] static bool isFactionMapPrefix(char prefix) {
        return prefix == 'R' || prefix == 'E' || prefix == 'U';
    }

    [[nodiscard]] static bool tryParseFactionHomeMap(std::string_view mapName,
                                                     char& prefixOut,
                                                     int32_t& mapNumberOut) {
        if (mapName.size() < 3 || mapName[1] != '-') {
            return false;
        }

        const char prefix = mapName[0];
        if (!isFactionMapPrefix(prefix)) {
            return false;
        }

        const char digit = mapName[2];
        if (digit < '1' || digit > '9') {
            return false;
        }

        prefixOut = prefix;
        mapNumberOut = digit - '0';
        return true;
    }

    [[nodiscard]] bool isNeverJumpPortalTarget(const std::string& targetMapName) const {
        return targetMapName.empty() ||
               isAvoidedMap(targetMapName) ||
               targetMapName == "T-1" ||
               targetMapName == "G-1";
    }

    [[nodiscard]] bool isOwnSafePortalTarget(const std::string& targetMapName) const {
        if (isNeverJumpPortalTarget(targetMapName)) {
            return false;
        }

        char targetPrefix = '\0';
        int32_t mapNumber = 0;
        if (!tryParseFactionHomeMap(targetMapName, targetPrefix, mapNumber) || mapNumber > 7) {
            return false;
        }

        const char ownPrefix = factionPrefixFor(engine_->hero().fraction);
        return ownPrefix != '\0' && targetPrefix == ownPrefix;
    }

    [[nodiscard]] bool isEnemyFactionPortalTarget(const std::string& targetMapName) const {
        if (isNeverJumpPortalTarget(targetMapName)) {
            return false;
        }

        char targetPrefix = '\0';
        int32_t mapNumber = 0;
        if (!tryParseFactionHomeMap(targetMapName, targetPrefix, mapNumber) || mapNumber > 7) {
            return false;
        }

        const char ownPrefix = factionPrefixFor(engine_->hero().fraction);
        return ownPrefix != '\0' && targetPrefix != ownPrefix;
    }

    [[nodiscard]] double nearestVisibleEnemyDistance(const Position& heroPos,
                                                     const EnemyFleeAssessment& assessment) const {
        double bestDistance = std::numeric_limits<double>::max();
        for (const auto& enemy : assessment.visibleEnemies) {
            bestDistance = std::min(
                bestDistance,
                heroPos.distanceTo(Position(enemy.lastX, enemy.lastY))
            );
        }

        if (bestDistance == std::numeric_limits<double>::max()) {
            return 0.0;
        }
        return bestDistance;
    }

    [[nodiscard]] bool shouldJumpEnemyFactionPortal(const Position& heroPos,
                                                    const EnemyFleeAssessment& assessment) const {
        for (const auto& enemy : assessment.visibleEnemies) {
            if (enemy.isAttackingUs) {
                return true;
            }
        }

        if (config_.fleeMode != SafetyFleeMode::OnEnemySeen || !assessment.active) {
            return false;
        }

        const double nearestEnemyDistance = nearestVisibleEnemyDistance(heroPos, assessment);
        return nearestEnemyDistance > 0.0 && nearestEnemyDistance < 800.0;
    }

    /**
     * @brief Check if a portal target map is unsafe (enemy faction, PvP, or avoided).
     *
     * Faction map prefixes: R=1, E=2, U=3.
     * A map is unsafe if it belongs to a different faction's home territory (X-1..X-7),
     * or is a known PvP map (T-1, G-1), or is in the avoided maps list.
     */
    [[nodiscard]] bool isUnsafePortalTarget(const std::string& targetMapName) const {
        return isNeverJumpPortalTarget(targetMapName) || isEnemyFactionPortalTarget(targetMapName);
    }
