    [[nodiscard]] MapBounds getMapBounds() const {
        MapBounds bounds;
        const auto info = engine_->mapInfo();
        if (info.width > 0 && info.height > 0) {
            bounds.maxX = static_cast<double>(info.width);
            bounds.maxY = static_cast<double>(info.height);
        }
        return bounds;
    }

    [[nodiscard]] const PortalInfo* findPortalById(const GameSnapshot& snap, int32_t portalId) const {
        for (const auto& portal : snap.entities.portals) {
            if (portal.id == portalId && portal.isWorldPortal()) {
                return &portal;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const StationInfo* findStationById(const GameSnapshot& snap, int32_t stationId) const {
        for (const auto& station : snap.entities.stations) {
            if (station.id == stationId) {
                return &station;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool isPreferredRepairStation(const StationInfo& station) const {
        return station.type == "SPACE_STATION";
    }

    void clearEscapeAnchor() {
        escapeAnchor_.reset();
        escapePortalId_ = 0;
        escapePortalMap_.clear();
        unsafePortalLogged_ = false;
    }

    void clearRepairAnchor() {
        repairAnchor_.reset();
        repairHoldIssued_ = false;
    }

    [[nodiscard]] bool isRepairAnchorReached(const GameSnapshot& snap) const {
        if (snap.inSafeZone) {
            return true;
        }
        if (!repairAnchor_.has_value()) {
            return false;
        }

        const double radius = repairAnchor_->kind == RepairAnchorKind::Station
            ? REPAIR_STATION_ARRIVAL_RADIUS
            : REPAIR_PORTAL_ARRIVAL_RADIUS;
        return Position(snap.hero.x, snap.hero.y).distanceTo(repairAnchor_->position) <= radius;
    }

    [[nodiscard]] bool isEscapeAnchorReached(const GameSnapshot& snap) const {
        if (!escapeAnchor_.has_value()) {
            return false;
        }

        if (escapeAnchor_->kind == RepairAnchorKind::Station && snap.inSafeZone) {
            return true;
        }

        const double radius = escapeAnchor_->kind == RepairAnchorKind::Station
            ? REPAIR_STATION_ARRIVAL_RADIUS
            : REPAIR_PORTAL_ARRIVAL_RADIUS;
        return Position(snap.hero.x, snap.hero.y).distanceTo(escapeAnchor_->position) <= radius;
    }

    [[nodiscard]] std::optional<std::pair<int32_t, const PortalInfo*>>
    findBestEscapePortal(const GameSnapshot& snap, int32_t excludedPortalId = 0) const {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const PortalInfo* bestSafe = nullptr;
        int32_t bestSafeId = 0;
        double bestSafeScore = std::numeric_limits<double>::max();
        const PortalInfo* bestFallback = nullptr;
        int32_t bestFallbackId = 0;
        double bestFallbackScore = std::numeric_limits<double>::max();

        for (const auto& portal : snap.entities.portals) {
            if (!portal.isWorldPortal() || portal.id == excludedPortalId) continue;

            const double score = heroPos.distanceTo(Position(portal.x, portal.y));
            const bool unsafe = isUnsafePortalTarget(portal.targetMapName);

            if (!unsafe && score < bestSafeScore) {
                bestSafeScore = score;
                bestSafe = &portal;
                bestSafeId = portal.id;
            }

            // Fallback: any portal (still useful as physical safe zone anchor)
            if (score < bestFallbackScore) {
                bestFallbackScore = score;
                bestFallback = &portal;
                bestFallbackId = portal.id;
            }
        }

        if (bestSafe) {
            return std::make_pair(bestSafeId, bestSafe);
        }
        if (bestFallback) {
            return std::make_pair(bestFallbackId, bestFallback);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RepairAnchor>
    findBestEscapeAnchor(const GameSnapshot& snap,
                         const ThreatSummary& summary,
                         RepairAnchorKind excludedKind = RepairAnchorKind::None,
                         int32_t excludedId = 0) const {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const auto assessment = assessEnemyFlee(snap, summary);

        std::optional<RepairAnchor> bestAnchor;
        double bestScore = std::numeric_limits<double>::max();
        for (const auto& station : snap.entities.stations) {
            if (!isPreferredRepairStation(station)) {
                continue;
            }
            if (excludedKind == RepairAnchorKind::Station && station.id == excludedId) {
                continue;
            }

            RepairAnchor anchor{
                .kind = RepairAnchorKind::Station,
                .id = station.id,
                .position = Position(station.x, station.y),
                .label = station.name.empty()
                    ? station.type
                    : station.name,
            };
            const double score = scoreEscapeAnchor(heroPos, anchor, assessment);
            if (score < bestScore) {
                bestScore = score;
                bestAnchor = anchor;
            }
        }

        for (const auto& portal : snap.entities.portals) {
            if (!portal.isWorldPortal()) {
                continue;
            }
            if (excludedKind == RepairAnchorKind::Portal && portal.id == excludedId) {
                continue;
            }

            RepairAnchor anchor{
                .kind = RepairAnchorKind::Portal,
                .id = portal.id,
                .position = Position(portal.x, portal.y),
                .label = portal.targetMapName.empty()
                    ? std::string("Portal")
                    : portal.targetMapName,
            };
            const double score = scoreEscapeAnchor(heroPos, anchor, assessment);
            if (score < bestScore) {
                bestScore = score;
                bestAnchor = anchor;
            }
        }

        return bestAnchor;
    }

    [[nodiscard]] std::optional<RepairAnchor> findBestRepairAnchor(const GameSnapshot& snap) const {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const StationInfo* bestPreferredStation = nullptr;
        double bestPreferredStationScore = std::numeric_limits<double>::max();

        for (const auto& station : snap.entities.stations) {
            const double score = heroPos.distanceTo(Position(station.x, station.y));
            if (isPreferredRepairStation(station)) {
                if (score < bestPreferredStationScore) {
                    bestPreferredStationScore = score;
                    bestPreferredStation = &station;
                }
            }
        }

        if (bestPreferredStation) {
            return RepairAnchor{
                .kind = RepairAnchorKind::Station,
                .id = bestPreferredStation->id,
                .position = Position(bestPreferredStation->x, bestPreferredStation->y),
                .label = bestPreferredStation->name.empty()
                    ? bestPreferredStation->type
                    : bestPreferredStation->name,
            };
        }

        if (auto portal = findBestEscapePortal(snap); portal.has_value()) {
            return RepairAnchor{
                .kind = RepairAnchorKind::Portal,
                .id = portal->first,
                .position = Position(portal->second->x, portal->second->y),
                .label = portal->second->targetMapName.empty()
                    ? std::string("Portal")
                    : portal->second->targetMapName,
            };
        }

        return std::nullopt;
    }

    bool selectRepairAnchor(const GameSnapshot& snap) {
        clearRepairAnchor();

        const auto anchor = findBestRepairAnchor(snap);
        if (!anchor.has_value()) {
            fleeTarget_ = Position(snap.hero.x, snap.hero.y);
            escapePortalId_ = 0;
            escapePortalMap_.clear();
            lastFleeDistance_ = 0.0;
            lastProgressTime_ = snap.timestampMs;
            return false;
        }

        repairAnchor_ = anchor;
        repairHoldIssued_ = false;
        fleeTarget_ = repairAnchor_->position;
        escapePortalId_ = repairAnchor_->kind == RepairAnchorKind::Portal ? repairAnchor_->id : 0;
        escapePortalMap_ = repairAnchor_->label;
        beginFleeProgress(snap, Position(snap.hero.x, snap.hero.y).distanceTo(fleeTarget_));
        return true;
    }

    bool selectEscapeAnchor(const GameSnapshot& snap,
                            const ThreatSummary& summary,
                            RepairAnchorKind excludedKind = RepairAnchorKind::None,
                            int32_t excludedId = 0) {
        clearEscapeAnchor();

        const auto anchor = findBestEscapeAnchor(snap, summary, excludedKind, excludedId);
        if (!anchor.has_value()) {
            fleeTarget_ = Position(snap.hero.x, snap.hero.y);
            lastFleeDistance_ = 0.0;
            lastProgressTime_ = snap.timestampMs;
            return false;
        }

        escapeAnchor_ = anchor;
        fleeTarget_ = escapeAnchor_->position;
        escapePortalId_ = escapeAnchor_->kind == RepairAnchorKind::Portal ? escapeAnchor_->id : 0;
        escapePortalMap_ = escapeAnchor_->label;
        beginFleeProgress(snap, Position(snap.hero.x, snap.hero.y).distanceTo(fleeTarget_));
        return true;
    }

    void beginFleeProgress(const GameSnapshot& snap, double initialDistance) {
        lastFleeDistance_ = initialDistance;
        lastProgressTime_ = snap.timestampMs;
    }

    void selectFallbackFleeTarget(const GameSnapshot& snap,
                                  const ThreatSummary& summary) {
        clearEscapeAnchor();

        const auto assessment = assessEnemyFlee(snap, summary);
        const EnemyInfo* threat = assessment.primaryThreat.has_value()
            ? &(*assessment.primaryThreat)
            : threatTracker_.getPrimaryThreat(
                snap.hero.x,
                snap.hero.y,
                snap.timestampMs,
                config_.enemySeenTimeoutMs
            );

        const MapBounds bounds = getMapBounds();
        if (threat) {
            fleeTarget_ = navigation_.fleeFromPosition(
                Position(snap.hero.x, snap.hero.y),
                Position(threat->lastX, threat->lastY),
                bounds
            );
        } else {
            fleeTarget_ = navigation_.randomPosition(bounds, 600);
        }

        beginFleeProgress(snap, Position(snap.hero.x, snap.hero.y).distanceTo(fleeTarget_));
    }
