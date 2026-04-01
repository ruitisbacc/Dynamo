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

    [[nodiscard]] EnemyFleeAssessment assessEnemyFlee(const GameSnapshot& snap,
                                                      const ThreatSummary& summary) const {
        EnemyFleeAssessment assessment;
        assessment.visibleEnemies = threatTracker_.freshEnemies(
            snap.timestampMs,
            config_.enemySeenTimeoutMs
        );

        const Position heroPos(snap.hero.x, snap.hero.y);
        if (summary.primaryThreatId != 0) {
            assessment.primaryThreat = findFreshEnemyById(summary.primaryThreatId, snap.timestampMs);
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
                if (summary.beingAttacked) {
                    assessment.active = true;
                    if (const auto* attacker = threatTracker_.getAttacker(snap.timestampMs);
                        attacker != nullptr) {
                        assessment.primaryThreat = *attacker;
                        break;
                    }
                }

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
            if (!anchor.label.empty() && isUnsafePortalTarget(anchor.label)) {
                score += 5000.0;  // Heavily penalize unsafe destinations
            } else if (!anchor.label.empty() && isAvoidedMap(anchor.label)) {
                score += 3200.0;
            } else {
                score -= 180.0;
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
        std::lock_guard<std::mutex> lock(telemetryMutex_);
        telemetry_.state = state_;
        telemetry_.threatLevel = summary.level;
        telemetry_.decision = std::string(decision);
        telemetry_.escapeMap = escapePortalMap_;
        telemetry_.escapePortalId = escapePortalId_;
        telemetry_.visibleEnemies = summary.visibleEnemies;
        telemetry_.closeEnemies = summary.closeEnemies;
        telemetry_.attackers = summary.attackers;
        telemetry_.primaryThreatId = summary.primaryThreatId;
        telemetry_.hpPercent = static_cast<float>(calculateHpPercent(snap));
        telemetry_.primaryThreatDistance = static_cast<float>(summary.primaryThreatDistance);
        telemetry_.fleeTargetX = static_cast<float>(fleeTarget_.x);
        telemetry_.fleeTargetY = static_cast<float>(fleeTarget_.y);
        telemetry_.beingAttacked = summary.beingAttacked;
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

    /**
     * @brief Check if a portal target map is unsafe (enemy faction, PvP, or avoided).
     *
     * Faction map prefixes: R=1, E=2, U=3.
     * A map is unsafe if it belongs to a different faction's home territory (X-1..X-4),
     * or is a known PvP map (T-1, G-1), or is in the avoided maps list.
     */
    [[nodiscard]] bool isUnsafePortalTarget(const std::string& targetMapName) const {
        if (targetMapName.empty()) {
            return true;  // Unknown destination = unsafe
        }
        if (isAvoidedMap(targetMapName)) {
            return true;
        }
        // PvP / contested maps are always unsafe for escape
        if (targetMapName == "T-1" || targetMapName == "G-1") {
            return true;
        }
        // Check faction-based safety: only jump to own faction's maps
        const int32_t heroFraction = engine_->hero().fraction;
        if (heroFraction <= 0) {
            return false;  // Unknown faction, can't filter
        }
        // Faction prefixes: 1=R, 2=E, 3=U
        char targetPrefix = targetMapName.empty() ? '\0' : targetMapName[0];
        char ownPrefix = '\0';
        switch (heroFraction) {
            case 1: ownPrefix = 'R'; break;
            case 2: ownPrefix = 'E'; break;
            case 3: ownPrefix = 'U'; break;
            default: return false;
        }
        // Enemy faction home maps (X-1 to X-4) are unsafe
        if (targetPrefix != ownPrefix && (targetPrefix == 'R' || targetPrefix == 'E' || targetPrefix == 'U')) {
            if (targetMapName.size() >= 3 && targetMapName[1] == '-') {
                char mapNum = targetMapName[2];
                if (mapNum >= '1' && mapNum <= '4') {
                    return true;
                }
            }
        }
        return false;
    }
