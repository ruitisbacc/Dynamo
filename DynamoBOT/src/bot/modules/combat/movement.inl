    [[nodiscard]] CombatTargetMotion buildTargetMotion(const Position& /*heroPos*/,
                                                       const ShipInfo& target,
                                                       double /*heroSpeed*/,
                                                       double /*desiredRange*/) const {
        CombatTargetMotion motion;
        motion.current = Position(target.x, target.y);
        motion.anchor = motion.current;

        // Determine effective NPC speed: prefer observed, fallback to server, then default
        const double effectiveNpcSpeed = target.observedSpeedEma > 10.0f
            ? std::clamp(static_cast<double>(target.observedSpeedEma), 90.0, 520.0)
            : (target.speed > 0
                ? std::clamp(static_cast<double>(target.speed), 90.0, 520.0)
                : 220.0);

        // Determine if NPC is actually moving (observed beats server flag)
        const bool observedMoving = target.observedSpeedEma > 15.0f;
        const bool serverMoving = target.isMoving &&
            (target.targetX != 0 || target.targetY != 0);
        if (!observedMoving && !serverMoving) {
            return motion;
        }

        // Direction: prefer observed velocity, fall back to targetX/Y path vector
        const double observedVLen = std::sqrt(
            static_cast<double>(target.observedVx) * target.observedVx +
            static_cast<double>(target.observedVy) * target.observedVy);
        Position moveDir;
        if (observedVLen > 10.0) {
            moveDir = Position(target.observedVx / observedVLen,
                               target.observedVy / observedVLen);
            // Compute remaining path from server target if available
            if (target.isMoving && (target.targetX != 0 || target.targetY != 0)) {
                motion.destination = Position(target.targetX, target.targetY);
                motion.pathRemaining = vectorLength(subtract(motion.destination, motion.current));
            }
        } else {
            motion.destination = Position(target.targetX, target.targetY);
            const Position pathVector = subtract(motion.destination, motion.current);
            const double pathLen = vectorLength(pathVector);
            if (pathLen < 12.0) {
                return motion;
            }
            moveDir = scale(pathVector, 1.0 / pathLen);
            motion.pathRemaining = pathLen;
        }

        motion.direction = moveDir;
        motion.moving = true;
        motion.speed = effectiveNpcSpeed;

        // Fixed 400ms prediction, capped by remaining path to avoid overshoot
        constexpr double kPredictionMs = 400.0;
        double leadDistance = effectiveNpcSpeed * (kPredictionMs / 1000.0);
        if (motion.pathRemaining > 0.0) {
            leadDistance = std::min(leadDistance, motion.pathRemaining);
        }
        motion.anchor = add(motion.current, scale(moveDir, leadDistance));
        motion.leadTime = kPredictionMs / 1000.0;
        return motion;
    }

    [[nodiscard]] Position predictedCombatTargetPosition(const Position& heroPos,
                                                         const ShipInfo& target,
                                                         double heroSpeed,
                                                         double desiredRange) const {
        return buildTargetMotion(heroPos, target, heroSpeed, desiredRange).anchor;
    }

    [[nodiscard]] double orbitNpcAvoidanceRadius(const ShipInfo& ship,
                                                 const GameSnapshot& snap,
                                                 double combatRange) const {
        double radius = std::clamp(combatRange * 0.55, 260.0, 520.0);
        if (ship.selectedTarget == snap.hero.id && ship.isAttacking) {
            radius += 120.0;
        }
        if (ship.isMoving) {
            radius += 40.0;
        }
        return radius;
    }

    [[nodiscard]] double scoreOrbitCandidate(const Position& candidate,
                                             const GameSnapshot& snap,
                                             const ShipInfo& target,
                                             const OrbitScoringContext& context,
                                             bool clockwise,
                                             bool preferredClockwise) const {
        const MapBounds bounds = currentBounds();
        if (!bounds.contains(candidate, 220.0)) {
            return -std::numeric_limits<double>::infinity();
        }

        const double distanceToAnchor = candidate.distanceTo(context.anchor);
        const double distanceToTarget = candidate.distanceTo(context.targetPos);
        const double desiredRangeError = std::abs(distanceToAnchor - context.desiredRange);
        const double correctiveError = std::abs(distanceToAnchor - context.correctiveRadius);
        const double travelDistance = context.heroPos.distanceTo(candidate);

        double score = 0.0;
        score -= desiredRangeError * 28.0;
        score -= correctiveError * 10.0;
        score -= std::abs(distanceToTarget - context.desiredRange) * 3.0;
        score -= travelDistance * 0.15;

        const double candidateAngle = std::atan2(candidate.y - context.anchor.y, candidate.x - context.anchor.x);
        const double delta = wrapAngle(candidateAngle - context.currentAngle);
        const double orientedDelta = clockwise ? -delta : delta;

        if (orientedDelta < 0.035) {
            score -= 1800.0 + std::abs(orientedDelta) * 550.0;
        } else {
            score += std::min(orientedDelta, 0.75) * 1450.0;
        }

        if (clockwise != preferredClockwise) {
            score -= 180.0;
        }

        const double edgeClearance = std::min({
            candidate.x - bounds.minX,
            bounds.maxX - candidate.x,
            candidate.y - bounds.minY,
            bounds.maxY - candidate.y
        });
        if (edgeClearance < 260.0) {
            score -= (260.0 - edgeClearance) * 18.0;
        } else {
            score += std::clamp(edgeClearance - 260.0, 0.0, 900.0) * 0.28;
        }

        if (context.collectApproachPoint.has_value()) {
            const double candidateToBox = candidate.distanceTo(*context.collectApproachPoint);
            const double heroToBox = context.heroPos.distanceTo(*context.collectApproachPoint);

            if (candidateToBox <= 240.0) {
                score += 1800.0;
            } else if (candidateToBox <= 620.0) {
                score += (620.0 - candidateToBox) * 3.4;
            }

            if (candidateToBox + 60.0 < heroToBox) {
                score += 260.0;
            }
        }

        for (const auto& ship : snap.entities.ships) {
            if (ship.id == 0 || ship.id == target.id || !ship.isNpc || ship.isDestroyed || ship.isCloaked) {
                continue;
            }

            const double hazardRadius = orbitNpcAvoidanceRadius(ship, snap, context.desiredRange);
            const Position shipPos(ship.x, ship.y);
            const double distanceToNpc = candidate.distanceTo(shipPos);
            const double segmentDistance = distanceToSegment(shipPos, context.heroPos, candidate);
            if (distanceToNpc < hazardRadius * 0.55) {
                score -= 15000.0 + (hazardRadius - distanceToNpc) * 50.0;
                continue;
            }
            if (distanceToNpc < hazardRadius) {
                score -= (hazardRadius - distanceToNpc) * 20.0;
            }
            if (segmentDistance < hazardRadius * 0.7) {
                score -= (hazardRadius * 0.7 - segmentDistance) * 14.0;
            }
        }

        return score;
    }

    [[nodiscard]] OrbitSolution evaluateOrbitDirection(const GameSnapshot& snap,
                                                       const ShipInfo& target,
                                                       const OrbitScoringContext& context,
                                                       double heroSpeed,
                                                       double npcSpeed,
                                                       bool clockwise,
                                                       bool preferredClockwise) const {
        OrbitSolution best;
        best.anchor = context.anchor;
        best.clockwise = clockwise;

        const Position fallback = navigation_.continuousOrbit(
            context.heroPos,
            context.anchor,
            context.correctiveRadius,
            heroSpeed,
            npcSpeed,
            clockwise
        );
        const double fallbackScore = scoreOrbitCandidate(
            fallback,
            snap,
            target,
            context,
            clockwise,
            preferredClockwise
        );
        best.point = fallback;
        best.score = fallbackScore;
        best.rangeError = std::abs(fallback.distanceTo(context.anchor) - context.desiredRange);
        best.edgeClearance = std::min({
            fallback.x - currentBounds().minX,
            currentBounds().maxX - fallback.x,
            fallback.y - currentBounds().minY,
            currentBounds().maxY - fallback.y
        });
        best.valid = std::isfinite(fallbackScore);

        const double currentDistanceToAnchor = context.heroPos.distanceTo(context.anchor);
        const double radialCorrectionDist =
            std::abs(currentDistanceToAnchor - context.desiredRange);
        const double linearBudget =
            heroSpeed * 0.72 + std::min(npcSpeed, 240.0) * 0.38;
        const double tangentialBudget = std::max(linearBudget - radialCorrectionDist, 0.0);
        const double angleStep = std::clamp(
            tangentialBudget / std::max(context.desiredRange, 100.0),
            0.03,
            0.34
        );
        const double scanStep = std::max(0.03, angleStep * 0.45);
        const double primaryAngle = context.currentAngle + (clockwise ? -angleStep : angleStep);

        // Chord compensation: the hero moves in a straight line to the orbit point,
        // cutting inside the circle. Inflate the orbit radius to counteract.
        const double halfAngle = std::min(angleStep * 0.5, 0.25);
        const double chordCompensation = context.desiredRange * (1.0 - std::cos(halfAngle));

        // Fine-grained radius offsets: dense near desired range, sparser further out
        const std::array<double, 9> radiusOffsets{
            -0.12, -0.06, -0.03, -0.01, 0.0, 0.01, 0.03, 0.06, 0.12
        };

        for (double radiusOffset : radiusOffsets) {
            const double radius = std::clamp(
                context.correctiveRadius + chordCompensation +
                    context.desiredRange * radiusOffset,
                context.desiredRange * 0.72,
                context.desiredRange * 1.28
            );

            for (int angleOffset = -6; angleOffset <= 6; ++angleOffset) {
                const double angle = primaryAngle + static_cast<double>(angleOffset) * scanStep;
                const Position candidate(
                    context.anchor.x + std::cos(angle) * radius,
                    context.anchor.y + std::sin(angle) * radius
                );

                const double score = scoreOrbitCandidate(
                    candidate,
                    snap,
                    target,
                    context,
                    clockwise,
                    preferredClockwise
                );

                if (!std::isfinite(score) || score <= best.score) {
                    continue;
                }

                best.point = candidate;
                best.score = score;
                best.rangeError = std::abs(candidate.distanceTo(context.anchor) - context.desiredRange);
                best.edgeClearance = std::min({
                    candidate.x - currentBounds().minX,
                    currentBounds().maxX - candidate.x,
                    candidate.y - currentBounds().minY,
                    currentBounds().maxY - candidate.y
                });
                best.valid = true;
            }
        }

        return best;
    }

    [[nodiscard]] OrbitSolution selectOrbitSolution(const GameSnapshot& snap,
                                                    const ShipInfo& target,
                                                    double combatRange,
                                                    double heroSpeed,
                                                    double npcSpeed,
                                                    bool preferredClockwise,
                                                    const BoxInfo* collectBox = nullptr) const {
        const Position heroPos(snap.hero.x, snap.hero.y);
        const CombatTargetMotion motion = buildTargetMotion(heroPos, target, heroSpeed, combatRange);
        const double currentDistanceToAnchor = heroPos.distanceTo(motion.anchor);

        OrbitScoringContext context;
        context.heroPos = heroPos;
        context.targetPos = Position(target.x, target.y);
        context.anchor = motion.anchor;
        context.desiredRange = combatRange;
        context.correctiveRadius = correctiveOrbitRadius(currentDistanceToAnchor, combatRange);
        context.currentAngle = std::atan2(heroPos.y - motion.anchor.y, heroPos.x - motion.anchor.x);
        if (collectBox && collectBox->existsOnMap) {
            context.collectApproachPoint = collectApproachPosition(*collectBox);
        }

        const OrbitSolution preferred = evaluateOrbitDirection(
            snap,
            target,
            context,
            heroSpeed,
            npcSpeed,
            preferredClockwise,
            preferredClockwise
        );
        const OrbitSolution alternate = evaluateOrbitDirection(
            snap,
            target,
            context,
            heroSpeed,
            npcSpeed,
            !preferredClockwise,
            preferredClockwise
        );

        if (!preferred.valid) {
            return alternate;
        }
        if (!alternate.valid) {
            return preferred;
        }

        const bool switchAllowed =
            snap.timestampMs - lastOrbitDirectionChangeAtMs_ >= ORBIT_DIRECTION_SWITCH_COOLDOWN_MS;
        const bool alternateClearlyBetter = alternate.score > preferred.score + 220.0;
        const bool alternateFixesRange =
            alternate.rangeError + rangeTolerance(context.desiredRange) * 0.8 < preferred.rangeError;
        const bool preferredCramped = preferred.edgeClearance < 260.0;
        const bool alternateLessCramped = alternate.edgeClearance > preferred.edgeClearance + 80.0;

        if ((alternateClearlyBetter && switchAllowed) ||
            ((alternateFixesRange || alternateLessCramped) &&
             alternate.score > preferred.score - 120.0 &&
             (switchAllowed || preferredCramped))) {
            return alternate;
        }

        return preferred;
    }

    [[nodiscard]] OrbitSolution selectApproachEntrySolution(const GameSnapshot& snap,
                                                            const ShipInfo& target,
                                                            double desiredRange,
                                                            double heroSpeed,
                                                            double npcSpeed,
                                                            bool preferredClockwise) const {
        OrbitSolution solution;
        const Position heroPos(snap.hero.x, snap.hero.y);
        const Position anchor = predictedCombatTargetPosition(heroPos, target, heroSpeed, desiredRange);
        const double anchorDistance = heroPos.distanceTo(anchor);
        solution.anchor = anchor;
        solution.clockwise = preferredClockwise;
        solution.valid = true;
        solution.rangeError = std::abs(anchorDistance - desiredRange);

        const Position directApproach = navigation_.approachPosition(heroPos, anchor, desiredRange);
        solution.point = directApproach;

        if (anchorDistance > desiredRange + std::max(260.0, desiredRange * 0.55)) {
            return solution;
        }

        const OrbitSolution orbitEntry = selectOrbitSolution(
            snap,
            target,
            desiredRange,
            heroSpeed,
            npcSpeed,
            preferredClockwise
        );
        if (!orbitEntry.valid) {
            return solution;
        }

        const double blend = std::clamp(
            1.0 - ((anchorDistance - desiredRange) / std::max(240.0, desiredRange * 0.45)),
            0.0,
            1.0
        );
        const double smoothBlend = blend * blend * (3.0 - 2.0 * blend);
        solution.point = directApproach.interpolate(orbitEntry.point, smoothBlend);
        solution.clockwise = orbitEntry.clockwise;
        solution.score = orbitEntry.score;
        solution.rangeError = orbitEntry.rangeError;
        return solution;
    }

    [[nodiscard]] Position selectCreateSpacePosition(const Position& heroPos,
                                                     const Position& anchor,
                                                     double desiredRange,
                                                     bool clockwise) const {
        const Position radial = normalizedOr(subtract(heroPos, anchor), Position(1.0, 0.0));
        const Position tangent = tangentFromRadial(radial, clockwise);
        const double currentDistance = heroPos.distanceTo(anchor);
        const double closenessRatio = std::clamp(
            (desiredRange - currentDistance) / std::max(desiredRange, 100.0),
            0.0,
            0.35
        );
        const double radialWeight = 0.82 + closenessRatio * 0.65;
        const double tangentialWeight = 0.48 - closenessRatio * 0.18;
        const Position escapeDirection = normalizedOr(
            add(scale(radial, radialWeight), scale(tangent, tangentialWeight)),
            radial
        );
        const double escapeRadius = desiredRange + std::max(55.0, desiredRange * 0.16);
        return add(anchor, scale(escapeDirection, escapeRadius));
    }

    [[nodiscard]] double idealCombatRange() const {
        const double attackRange = static_cast<double>(config_.attackRange);
        const double followDistance = static_cast<double>(config_.followDistance);
        return std::clamp(followDistance, 420.0, std::max(460.0, attackRange - 40.0));
    }
