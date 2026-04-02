    void startFleeing(const GameSnapshot& snap,
                      const ThreatSummary& summary,
                      std::string_view reason = "Threat") {
        transitionTo(SafetyState::Fleeing, snap.timestampMs);
        clearRepairAnchor();

        if (snap.inSafeZone) {
            clearEscapeAnchor();
            fleeTarget_ = Position(snap.hero.x, snap.hero.y);
            beginFleeProgress(snap, 0.0);
            transitionTo(SafetyState::AtPortal, snap.timestampMs);
            publishTelemetry(snap, summary, "AlreadySafeZone");
            return;
        }

        if (config_.useEscapeConfig && snap.hero.activeConfig != config_.escapeConfigId) {
            switchConfig(config_.escapeConfigId);
        }

        if (config_.preferPortalEscape) {
            if (selectEscapeAnchor(snap, summary)) {
                std::cout << "[Safety] Fleeing to " << repairAnchorKindName(escapeAnchor_->kind)
                          << " at (" << fleeTarget_.x << ", " << fleeTarget_.y << ")\n";
                publishTelemetry(snap, summary, reason);
                return;
            }
        }

        selectFallbackFleeTarget(snap, summary);
        std::cout << "[Safety] Fleeing to (" << fleeTarget_.x << ", " << fleeTarget_.y << ")\n";
        publishTelemetry(snap, summary, reason);
    }

    void startRepairing(const GameSnapshot& snap,
                        const ThreatSummary& summary,
                        std::string_view reason = "Repairing") {
        transitionTo(SafetyState::Repairing, snap.timestampMs);
        if (movement_) {
            movement_->release(name());
        }
        repairHoldIssued_ = false;

        if (config_.useEscapeConfig && snap.hero.activeConfig != config_.escapeConfigId) {
            switchConfig(config_.escapeConfigId);
        }

        if (snap.inSafeZone) {
            clearRepairAnchor();
            fleeTarget_ = Position(snap.hero.x, snap.hero.y);
            lastProgressTime_ = snap.timestampMs;
            std::cout << "[Safety] HP low, holding repair position inside safe zone\n";
            publishTelemetry(snap, summary, reason);
            return;
        }

        if (selectRepairAnchor(snap)) {
            const auto anchorKind = repairAnchor_->kind == RepairAnchorKind::Station
                ? "station"
                : "portal";
            std::cout << "[Safety] HP low, moving to repair " << anchorKind
                      << " at (" << repairAnchor_->position.x << ", "
                      << repairAnchor_->position.y << ")\n";
        } else {
            std::cout << "[Safety] HP low, no repair anchor found - holding current position\n";
        }

        publishTelemetry(snap, summary, reason);
    }

    void retargetEscape(const GameSnapshot& snap,
                        const ThreatSummary& summary,
                        std::string_view reason) {
        if (movement_) {
            movement_->release(name());
        }

        if (escapeAnchor_.has_value()) {
            if (selectEscapeAnchor(snap, summary, escapeAnchor_->kind, escapeAnchor_->id)) {
                fleeRetargetCount_++;
                std::cout << "[Safety] Retargeting escape "
                          << repairAnchorKindName(escapeAnchor_->kind)
                          << " to " << escapePortalMap_ << "\n";
                publishTelemetry(snap, summary, reason);
                return;
            }
        }

        fleeRetargetCount_++;
        selectFallbackFleeTarget(snap, summary);
        publishTelemetry(snap, summary, reason);
    }

    void handleMonitoring(const GameSnapshot& snap,
                          double hpPercent,
                          const ThreatSummary& summary) {
        const auto assessment = assessEnemyFlee(snap, summary);
        if (assessment.active || hpPercent < config_.minHpPercent) {
            startFleeing(snap, summary, "StartFlee");
            return;
        }

        if (hpPercent < config_.repairHpPercent &&
            !shouldDelayRepairForNpcKill(snap, hpPercent, assessment)) {
            startRepairing(snap, summary, "Repairing");
            return;
        }

        publishTelemetry(
            snap,
            summary,
            shouldDelayRepairForNpcKill(snap, hpPercent, assessment)
                ? "FinishNpcBeforeRepair"
                : "Monitoring"
        );
    }

    void handleFleeing(const GameSnapshot& snap,
                       double hpPercent,
                       const ThreatSummary& summary) {
        const auto assessment = assessEnemyFlee(snap, summary);
        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);

        if (escapeAnchor_.has_value()) {
            if (escapeAnchor_->kind == RepairAnchorKind::Portal) {
                if (const auto* portal = findPortalById(snap, escapeAnchor_->id)) {
                    fleeTarget_ = Position(portal->x, portal->y);
                    escapePortalId_ = portal->id;
                    escapePortalMap_ = portal->targetMapName.empty()
                        ? std::string("Portal")
                        : portal->targetMapName;
                } else {
                    retargetEscape(snap, summary, "PortalLost");
                    return;
                }
            } else if (escapeAnchor_->kind == RepairAnchorKind::Station) {
                if (const auto* station = findStationById(snap, escapeAnchor_->id)) {
                    fleeTarget_ = Position(station->x, station->y);
                    escapePortalId_ = 0;
                    escapePortalMap_ = station->name.empty()
                        ? station->type
                        : station->name;
                } else {
                    retargetEscape(snap, summary, "StationLost");
                    return;
                }
            } else {
                clearEscapeAnchor();
            }
        }

        const double distToTarget = heroPos.distanceTo(fleeTarget_);
        if (distToTarget + 60.0 < lastFleeDistance_) {
            lastFleeDistance_ = distToTarget;
            lastProgressTime_ = now;
        }

        if (escapeAnchor_.has_value() && isEscapeAnchorReached(snap)) {
            transitionTo(SafetyState::AtPortal, now);
            std::cout << "[Safety] Reached escape "
                      << repairAnchorKindName(escapeAnchor_->kind) << "\n";
            publishTelemetry(
                snap,
                summary,
                escapeAnchor_->kind == RepairAnchorKind::Station ? "AtStation" : "AtPortal"
            );
            return;
        }

        if (!assessment.active && hpPercent >= config_.repairHpPercent) {
            transitionTo(SafetyState::Monitoring, now);
            if (movement_) {
                movement_->release(name());
            }
            std::cout << "[Safety] Threat cleared, returning to monitoring\n";
            publishTelemetry(snap, summary, "ThreatCleared");
            return;
        }

        if (lastProgressTime_ > 0 && now - lastProgressTime_ > FLEE_STALL_TIMEOUT_MS) {
            retargetEscape(snap, summary, "FleeStalled");
            return;
        }

        if (now - lastMoveTime_ >= config_.fleeMoveCooldownMs) {
            lastMoveTime_ = now;

            if (!escapeAnchor_.has_value()) {
                const EnemyInfo* threat = assessment.primaryThreat.has_value()
                    ? &(*assessment.primaryThreat)
                    : threatTracker_.getPrimaryThreat(
                        snap.hero.x,
                        snap.hero.y,
                        snap.timestampMs,
                        config_.enemySeenTimeoutMs
                    );

                if (threat) {
                    fleeTarget_ = navigation_.fleeFromPosition(
                        Position(snap.hero.x, snap.hero.y),
                        Position(threat->lastX, threat->lastY),
                        getMapBounds()
                    );
                }
            }

            if (movement_) {
                movement_->move(name(), snap, fleeTarget_, MoveIntent::Escape);
            } else {
                engine_->moveTo(static_cast<float>(fleeTarget_.x),
                                static_cast<float>(fleeTarget_.y));
            }
        }

        publishTelemetry(
            snap,
            summary,
            escapeAnchor_.has_value()
                ? (escapeAnchor_->kind == RepairAnchorKind::Station ? "FleeStation" : "FleePortal")
                : "FleeOpenSpace"
        );
    }

    void handleAtPortal(const GameSnapshot& snap,
                        double hpPercent,
                        const ThreatSummary& summary) {
        const auto assessment = assessEnemyFlee(snap, summary);
        const Position heroPos(snap.hero.x, snap.hero.y);
        const bool holdingStation =
            escapeAnchor_.has_value() && escapeAnchor_->kind == RepairAnchorKind::Station;
        const bool atPortal =
            escapeAnchor_.has_value() &&
            escapeAnchor_->kind == RepairAnchorKind::Portal;
        const bool jumpToOwnSafeMap = atPortal && isOwnSafePortalTarget(escapePortalMap_);
        const bool jumpToEnemyFactionMap =
            atPortal &&
            isEnemyFactionPortalTarget(escapePortalMap_) &&
            shouldJumpEnemyFactionPortal(heroPos, assessment);
        const bool shouldJumpPortal = jumpToOwnSafeMap || jumpToEnemyFactionMap;

        if (atPortal && shouldJumpPortal) {
            std::cout << "[Safety] Jumping through portal to " << escapePortalMap_ << "\n";
            // Clear stale actions BEFORE sending teleport so the teleport
            // isn't accidentally flushed along with old movement commands.
            if (movement_) {
                movement_->release(name());
            }
            engine_->clearPendingActions();
            engine_->teleport();
            // Hold in AwaitingTeleport until map actually changes
            teleportFromMap_ = snap.mapName;
            teleportSentAtMs_ = snap.timestampMs;
            transitionTo(SafetyState::AwaitingTeleport, snap.timestampMs);
            publishTelemetry(snap, summary, "PortalJump");
            return;
        }

        if (atPortal && !shouldJumpPortal && !unsafePortalLogged_) {
            std::cout << "[Safety] Holding at portal to " << escapePortalMap_
                      << " because jump conditions are not met\n";
            unsafePortalLogged_ = true;
        }

        if (!assessment.active && hpPercent >= config_.repairHpPercent) {
            if (movement_) {
                movement_->release(name());
            }
            transitionTo(SafetyState::Monitoring, snap.timestampMs);
            clearEscapeAnchor();
            std::cout << "[Safety] Safe at anchor, returning to normal operation\n";
            publishTelemetry(
                snap,
                summary,
                holdingStation ? "SafeAtStation" : "SafeAtPortal"
            );
            return;
        }

        publishTelemetry(
            snap,
            summary,
            escapeAnchor_.has_value()
                ? (holdingStation ? "HoldingStation" : "HoldingPortal")
                : "HoldingSafeZone"
        );
    }

    void handleAwaitingTeleport(const GameSnapshot& snap,
                                double /*hpPercent*/,
                                const ThreatSummary& summary) {
        // Map changed = teleport completed
        if (snap.mapName != teleportFromMap_ && !snap.mapName.empty()) {
            std::cout << "[Safety] Teleport complete: " << teleportFromMap_
                      << " -> " << snap.mapName << "\n";
            threatTracker_.clear();
            clearEscapeAnchor();
            fleeTarget_ = Position{};
            lastFleeDistance_ = std::numeric_limits<double>::max();
            fleeRetargetCount_ = 0;
            teleportFromMap_.clear();
            teleportSentAtMs_ = 0;
            transitionTo(SafetyState::Monitoring, snap.timestampMs);
            publishTelemetry(snap, summary, "TeleportComplete");
            return;
        }

        // Timeout — teleport didn't happen (maybe moved away from portal)
        if (snap.timestampMs - teleportSentAtMs_ > TELEPORT_TIMEOUT_MS) {
            std::cout << "[Safety] Teleport timed out, returning to monitoring\n";
            teleportFromMap_.clear();
            teleportSentAtMs_ = 0;
            transitionTo(SafetyState::Monitoring, snap.timestampMs);
            publishTelemetry(snap, summary, "TeleportTimeout");
            return;
        }

        // While waiting: don't issue any movement, just hold
        publishTelemetry(snap, summary, "AwaitingTeleport");
    }

    void handleRepairing(const GameSnapshot& snap,
                         double hpPercent,
                         const ThreatSummary& summary) {
        const auto assessment = assessEnemyFlee(snap, summary);
        const auto now = snap.timestampMs;
        const Position heroPos(snap.hero.x, snap.hero.y);

        if (hpPercent >= config_.fullHpPercent) {
            if (movement_) {
                movement_->release(name());
            }
            clearRepairAnchor();
            transitionTo(SafetyState::Monitoring, snap.timestampMs);
            std::cout << "[Safety] Repair complete (" << hpPercent << "%)\n";

            if (config_.useEscapeConfig && snap.hero.activeConfig != config_.fightConfigId) {
                switchConfig(config_.fightConfigId);
            }

            publishTelemetry(snap, summary, "RepairComplete");
            return;
        }

        if (assessment.active &&
            !(reviveGraceUntilMs_ > 0 && now < reviveGraceUntilMs_)) {
            clearRepairAnchor();
            startFleeing(snap, summary, "RepairThreat");
            return;
        }

        if (config_.useEscapeConfig && snap.hero.activeConfig != config_.escapeConfigId) {
            switchConfig(config_.escapeConfigId);
        }

        // During revive grace period, treat as safe zone — hold position
        // until server confirms inSafeZone (avoids flying away to portal)
        const bool effectiveSafeZone = snap.inSafeZone ||
            (reviveGraceUntilMs_ > 0 && now < reviveGraceUntilMs_);

        // Clear grace once server confirms safe zone
        if (snap.inSafeZone && reviveGraceUntilMs_ > 0) {
            reviveGraceUntilMs_ = 0;
        }

        if (!effectiveSafeZone) {
            bool reacquireAnchor = false;
            if (!repairAnchor_.has_value()) {
                reacquireAnchor = true;
            } else if (repairAnchor_->kind == RepairAnchorKind::Portal &&
                       findPortalById(snap, repairAnchor_->id) == nullptr) {
                reacquireAnchor = true;
            } else if (repairAnchor_->kind == RepairAnchorKind::Station &&
                       findStationById(snap, repairAnchor_->id) == nullptr) {
                reacquireAnchor = true;
            }

            if (reacquireAnchor && selectRepairAnchor(snap)) {
                const auto anchorKind = repairAnchor_->kind == RepairAnchorKind::Station
                    ? "station"
                    : "portal";
                std::cout << "[Safety] Reacquired repair " << anchorKind
                          << " at (" << repairAnchor_->position.x << ", "
                          << repairAnchor_->position.y << ")\n";
            }
        }

        if (repairAnchor_.has_value() && !isRepairAnchorReached(snap)) {
            const double distToAnchor = heroPos.distanceTo(repairAnchor_->position);
            if (distToAnchor + REPAIR_PROGRESS_DELTA < lastFleeDistance_) {
                lastFleeDistance_ = distToAnchor;
                lastProgressTime_ = now;
            }

            if (lastProgressTime_ > 0 && now - lastProgressTime_ > REPAIR_STALL_TIMEOUT_MS) {
                if (selectRepairAnchor(snap)) {
                    std::cout << "[Safety] Repair path stalled, retargeting anchor\n";
                } else {
                    std::cout << "[Safety] Repair path stalled, no alternate anchor found\n";
                }
            }

            if (now - lastMoveTime_ >= config_.fleeMoveCooldownMs) {
                lastMoveTime_ = now;
                if (movement_) {
                    movement_->move(name(), snap, repairAnchor_->position, MoveIntent::Escape);
                } else {
                    engine_->moveTo(static_cast<float>(repairAnchor_->position.x),
                                    static_cast<float>(repairAnchor_->position.y));
                }
            }

            publishTelemetry(
                snap,
                summary,
                repairAnchor_->kind == RepairAnchorKind::Station
                    ? "MoveRepairStation"
                    : "MoveRepairPortal"
            );
            return;
        }

        if (movement_) {
            movement_->release(name());
        }

        // During revive grace period, do NOT issue any movement — the hero
        // snapshot may still contain the pre-death position and sending a
        // moveTo would fly the ship back to the old flee target.
        if (reviveGraceUntilMs_ > 0 && now < reviveGraceUntilMs_) {
            publishTelemetry(snap, summary, "ReviveGrace");
            return;
        }

        const Position holdPos = repairAnchor_.has_value()
            ? repairAnchor_->position
            : Position(snap.hero.x, snap.hero.y);
        if (!repairHoldIssued_ ||
            (snap.hero.isMoving && now - lastMoveTime_ >= REPAIR_HOLD_REISSUE_MS)) {
            engine_->moveTo(static_cast<float>(holdPos.x), static_cast<float>(holdPos.y));
            repairHoldIssued_ = true;
            lastMoveTime_ = now;
        }

        publishTelemetry(
            snap,
            summary,
            snap.inSafeZone ? "RepairSafeZone" : "RepairHoldingAnchor"
        );
    }

    void handleConfigSwitching(const GameSnapshot& snap,
                               const ThreatSummary& summary) {
        transitionTo(SafetyState::Monitoring, snap.timestampMs);
        publishTelemetry(snap, summary, "ConfigSwitch");
    }

    void switchConfig(int32_t configId) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count() / 1000000;
        if (now - lastConfigSwitchTime_ < config_.configSwitchCooldownMs) {
            return;
        }

        lastConfigSwitchTime_ = now;
        engine_->switchConfig(configId);
        std::cout << "[Safety] Switching to config " << configId << "\n";
    }
