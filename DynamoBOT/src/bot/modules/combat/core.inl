    [[nodiscard]] MapBounds currentBounds() const {
        MapBounds bounds;
        const auto info = engine_->mapInfo();
        if (info.width > 0) {
            bounds.maxX = static_cast<double>(info.width);
        }
        if (info.height > 0) {
            bounds.maxY = static_cast<double>(info.height);
        }
        return bounds;
    }

    [[nodiscard]] double effectiveMaxDistance(const NpcTargetConfig& target) const {
        const double globalMaxDistance = config_.maxCombatDistance > 0
            ? static_cast<double>(config_.maxCombatDistance)
            : 0.0;
        const double perTargetMaxDistance = target.maxDistance > 0
            ? static_cast<double>(target.maxDistance)
            : 0.0;

        if (globalMaxDistance > 0.0 && perTargetMaxDistance > 0.0) {
            return std::min(globalMaxDistance, perTargetMaxDistance);
        }
        return globalMaxDistance > 0.0 ? globalMaxDistance : perTargetMaxDistance;
    }

    void moveWithinBounds(const GameSnapshot& snap,
                          const Position& requestedTarget,
                          MoveIntent intent) {
        const Position clampedTarget = currentBounds().clamp(requestedTarget, 220.0);
        if (movement_) {
            movement_->move(name(), snap, clampedTarget, intent);
        } else {
            engine_->moveTo(static_cast<float>(clampedTarget.x),
                            static_cast<float>(clampedTarget.y));
        }
    }

    void stopCombatActions(bool releaseMovement, bool clearSelection) {
        if (currentTargetId_ != 0 || state_ == CombatState::Attacking || state_ == CombatState::AwaitingKill) {
            engine_->stopAttack();
        }
        disableAutoRocket(0, true);
        if (clearSelection && currentTargetId_ != 0) {
            engine_->lockTarget(0);
        }
        if (releaseMovement && movement_) {
            movement_->release(name());
        }
    }

    void pruneTargetLockouts(int64_t now) {
        for (auto it = targetLockouts_.begin(); it != targetLockouts_.end();) {
            if (it->second.untilMs <= now) {
                it = targetLockouts_.erase(it);
            } else {
                ++it;
            }
        }
    }

    [[nodiscard]] bool isTargetLockedOut(int32_t targetId, int64_t now) const {
        auto it = targetLockouts_.find(targetId);
        return it != targetLockouts_.end() && it->second.untilMs > now;
    }

    void applyTargetLockout(int32_t targetId,
                            int64_t now,
                            std::string_view reason,
                            int64_t lockoutMs = TARGET_LOCKOUT_MS) {
        if (targetId == 0) {
            return;
        }

        auto& lockout = targetLockouts_[targetId];
        ++lockout.failures;
        const int64_t duration = lockoutMs + static_cast<int64_t>(lockout.failures - 1) * 2500;
        lockout.untilMs = std::max(lockout.untilMs, now + duration);

        std::cout << "[Combat] Locking out target " << targetId
                  << " for " << (duration / 1000.0) << "s"
                  << " (" << reason << ")\n";
    }

    void clearCurrentTargetState() {
        currentTargetId_ = 0;
        currentAmmoType_ = 1;
        currentRocketType_ = 0;
        currentMovementMode_ = CombatMovementMode::Adaptive;
        trackedTargetId_ = 0;
        currentTargetDurability_ = 0;
        currentTargetRecoveries_ = 0;
        lastTrackedTargetDistance_ = std::numeric_limits<double>::max();
        currentTargetAcquiredAtMs_ = 0;
        lastProgressTime_ = 0;
        lastDamageProgressAtMs_ = 0;
        recoveryUntilMs_ = 0;
        lastRecoveryAtMs_ = 0;
        lastOrbitDirectionChangeAtMs_ = 0;
        mixedUseKite_ = false;
        mixedMovementSwitchAtMs_ = 0;
        orbitClockwise_ = true;
    }

    [[nodiscard]] double targetSelectionRange() const {
        const double attackEnvelope = static_cast<double>(config_.attackRange) + 140.0;
        const double followEnvelope = static_cast<double>(config_.followDistance) + 200.0;
        return std::max(750.0, std::max(attackEnvelope, followEnvelope));
    }

    void ensureCombatConfig(const GameSnapshot& snap) {
        if (config_.configId < 1 || config_.configId > 2) {
            return;
        }
        if (snap.hero.activeConfig == config_.configId) {
            return;
        }
        if (snap.timestampMs - lastConfigSwitchTime_ < CONFIG_SWITCH_COOLDOWN_MS) {
            return;
        }
        lastConfigSwitchTime_ = snap.timestampMs;
        engine_->switchConfig(config_.configId);
    }

    void compileTargetPatterns() {
        targetPatterns_.clear();
        for (const auto& target : config_.targets) {
            try {
                targetPatterns_.emplace_back(target.namePattern, std::regex::icase);
            } catch (const std::regex_error&) {
                std::cerr << "[Combat] Invalid regex pattern: " << target.namePattern << "\n";
                // Fallback to exact match
                targetPatterns_.emplace_back(regex_escape(target.namePattern), std::regex::icase);
            }
        }
    }
    
    static std::string regex_escape(const std::string& s) {
        static const std::regex special_chars{R"([-[\]{}()*+?.,\^$|#\s])"};
        return std::regex_replace(s, special_chars, R"(\$&)");
    }

    [[nodiscard]] static bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (std::size_t i = 0; i < lhs.size(); ++i) {
            const auto left = static_cast<unsigned char>(lhs[i]);
            const auto right = static_cast<unsigned char>(rhs[i]);
            if (std::tolower(left) != std::tolower(right)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static std::string_view movementModeName(CombatMovementMode mode) {
        switch (mode) {
            case CombatMovementMode::Adaptive: return "Adaptive";
            case CombatMovementMode::Direct:
            case CombatMovementMode::Orbit:
            case CombatMovementMode::Kite:
            case CombatMovementMode::Mixed:
            case CombatMovementMode::Default:
            default:
                return "Adaptive";
        }
    }

    [[nodiscard]] static double wrapAngle(double angle) {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kTwoPi = kPi * 2.0;
        while (angle <= -kPi) {
            angle += kTwoPi;
        }
        while (angle > kPi) {
            angle -= kTwoPi;
        }
        return angle;
    }

    [[nodiscard]] static Position add(const Position& lhs, const Position& rhs) {
        return Position(lhs.x + rhs.x, lhs.y + rhs.y);
    }

    [[nodiscard]] static Position subtract(const Position& lhs, const Position& rhs) {
        return Position(lhs.x - rhs.x, lhs.y - rhs.y);
    }

    [[nodiscard]] static Position scale(const Position& value, double factor) {
        return Position(value.x * factor, value.y * factor);
    }

    [[nodiscard]] static double vectorLength(const Position& value) {
        return std::sqrt(value.x * value.x + value.y * value.y);
    }

    [[nodiscard]] static Position normalizedOr(const Position& value, const Position& fallback) {
        const double length = vectorLength(value);
        if (length < 1e-6) {
            return fallback;
        }
        return scale(value, 1.0 / length);
    }

    [[nodiscard]] static double dot(const Position& lhs, const Position& rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y;
    }

    [[nodiscard]] static Position tangentFromRadial(const Position& radial, bool clockwise) {
        return clockwise
            ? Position(-radial.y, radial.x)
            : Position(radial.y, -radial.x);
    }

    [[nodiscard]] static double distanceToSegment(const Position& point,
                                                  const Position& segmentStart,
                                                  const Position& segmentEnd) {
        const Position segment = subtract(segmentEnd, segmentStart);
        const double segmentLengthSquared = segment.x * segment.x + segment.y * segment.y;
        if (segmentLengthSquared < 1e-6) {
            return point.distanceTo(segmentStart);
        }

        const Position pointVector = subtract(point, segmentStart);
        const double factor = std::clamp(
            dot(pointVector, segment) / segmentLengthSquared,
            0.0,
            1.0
        );
        const Position projection = add(segmentStart, scale(segment, factor));
        return point.distanceTo(projection);
    }

    [[nodiscard]] static double rangeTolerance(double desiredRange) {
        return std::max(18.0, desiredRange * 0.035);
    }

    [[nodiscard]] static double rangeSlack(double desiredRange) {
        return std::max(40.0, desiredRange * 0.07);
    }

    [[nodiscard]] static double correctiveOrbitRadius(double currentDistance,
                                                      double desiredRange) {
        // DarkBot-style: 100% of distance error, capped at ±50% of range
        const double radiusError = currentDistance - desiredRange;
        const double correction = std::clamp(
            radiusError,
            -desiredRange * 0.5,
            desiredRange * 0.5
        );
        return desiredRange - correction;
    }

