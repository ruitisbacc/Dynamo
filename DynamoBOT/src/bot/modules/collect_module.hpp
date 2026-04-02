#pragma once

/**
 * @file collect_module.hpp
 * @brief Collection module - handles box collection
 *
 * Collects bonus, cargo, and green boxes from the map.
 */

#include "bot/core/module.hpp"
#include "bot/core/bot_config.hpp"
#include "bot/support/collection_helpers.hpp"
#include "bot/support/npc_database.hpp"
#include "bot/support/movement_controller.hpp"
#include "game/game_engine.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace dynamo {

/**
 * @brief Collection state machine states
 */
enum class CollectState {
    Searching,      // Looking for box
    Approaching,    // Moving towards box
    Collecting,     // At box, collecting
    Waiting         // Waiting for collection to complete
};

/**
 * @brief Box collection module
 *
 * Responsibilities:
 * - Find best box to collect based on priority
 * - Move to box position
 * - Collect box
 * - Handles dedicated collection outside of combat
 * - Blacklist repeatedly failing boxes for a short time
 */
class CollectModule : public Module {
public:
    CollectModule(std::shared_ptr<GameEngine> engine,
                  std::shared_ptr<MovementController> movement,
                  const CollectConfig& config)
        : Module(std::move(engine), std::move(movement))
        , config_(config) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "Collect";
    }

    [[nodiscard]] int getPriority(const GameSnapshot& snap) override {
        if (!config_.enabled || !enabled_) {
            return 0;
        }

        if (currentBoxId_ != 0 &&
            (state_ == CollectState::Approaching ||
             state_ == CollectState::Collecting ||
             state_ == CollectState::Waiting)) {
            return config_.priority;
        }

        if (auto box = findBestBox(snap); box.has_value()) {
            return config_.priority;
        }

        return 0;
    }

    void onStart() override {
        state_ = CollectState::Searching;
        lastMoveTime_ = 0;
        lastCollectTime_ = 0;
        resetCurrentTarget(false);
        std::cout << "[Collect] Module activated\n";
    }

    void onStop() override {
        lastMoveTime_ = 0;
        lastCollectTime_ = 0;
        resetCurrentTarget(true);
        state_ = CollectState::Searching;
        std::cout << "[Collect] Module deactivated\n";
    }

    void tick(const GameSnapshot& snap) override {
        ensureCollectionConfig(snap);
        pruneFailureState(snap.timestampMs);

        switch (state_) {
            case CollectState::Searching:
                handleSearching(snap);
                break;

            case CollectState::Approaching:
                handleApproaching(snap);
                break;

            case CollectState::Collecting:
                handleCollecting(snap);
                break;

            case CollectState::Waiting:
                handleWaiting(snap);
                break;
        }
    }

    [[nodiscard]] CollectState getState() const noexcept { return state_; }
    [[nodiscard]] int32_t getCurrentBoxId() const noexcept { return currentBoxId_; }

private:
    struct BoxFailureState {
        int32_t failedAttempts{0};
        int64_t ignoreUntilMs{0};
    };

    struct BoxSelection {
        int32_t id{0};
        const BoxInfo* box{nullptr};
    };

    CollectConfig config_;
    CollectState state_{CollectState::Searching};

    int32_t currentBoxId_{0};
    int64_t lastMoveTime_{0};
    int64_t lastCollectTime_{0};
    int64_t collectStartTime_{0};
    int64_t lastConfigSwitchTime_{0};
    int64_t currentBoxAcquiredAtMs_{0};
    int64_t lastBoxProgressAtMs_{0};
    int64_t currentCollectWaitUntilMs_{0};
    double lastBoxDistance_{std::numeric_limits<double>::infinity()};
    std::unordered_map<int32_t, BoxFailureState> failedBoxes_;

    static constexpr int64_t CONFIG_SWITCH_COOLDOWN_MS = 1000;
    static constexpr int64_t APPROACH_STALL_TIMEOUT_MS = 4000;
    static constexpr int64_t APPROACH_MAX_AGE_MS = 15000;
    static constexpr int64_t FAILED_BOX_IGNORE_MS = 120000;
    static constexpr int32_t MAX_FAILED_ATTEMPTS = 2;
    static constexpr double COLLECT_RANGE = 200.0;
    static constexpr double PROGRESS_DISTANCE_THRESHOLD = 25.0;

    void ensureCollectionConfig(const GameSnapshot& snap) {
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

    void pruneFailureState(int64_t now) {
        for (auto it = failedBoxes_.begin(); it != failedBoxes_.end();) {
            if (it->second.ignoreUntilMs > 0 && it->second.ignoreUntilMs <= now) {
                it->second.failedAttempts = 0;
                it->second.ignoreUntilMs = 0;
            }

            if (it->second.failedAttempts <= 0 && it->second.ignoreUntilMs <= 0) {
                it = failedBoxes_.erase(it);
            } else {
                ++it;
            }
        }
    }

    [[nodiscard]] bool isBoxIgnored(int32_t boxId, int64_t now) const {
        auto it = failedBoxes_.find(boxId);
        return it != failedBoxes_.end() && it->second.ignoreUntilMs > now;
    }

    void resetCurrentTarget(bool releaseMovement) {
        if (releaseMovement && movement_) {
            movement_->release(name());
        }
        currentBoxId_ = 0;
        collectStartTime_ = 0;
        currentBoxAcquiredAtMs_ = 0;
        lastBoxProgressAtMs_ = 0;
        currentCollectWaitUntilMs_ = 0;
        lastBoxDistance_ = std::numeric_limits<double>::infinity();
    }

    void beginTrackingBox(const BoxInfo& box, const GameSnapshot& snap) {
        Position heroPos(snap.hero.x, snap.hero.y);
        currentBoxAcquiredAtMs_ = snap.timestampMs;
        lastBoxProgressAtMs_ = snap.timestampMs;
        lastBoxDistance_ = heroPos.distanceTo(Position(box.x, box.y));
    }

    void noteCollectionSuccess(int32_t boxId) {
        failedBoxes_.erase(boxId);
        std::cout << "[Collect] Box collected\n";
    }

    void markBoxFailure(int32_t boxId, int64_t now, std::string_view reason) {
        if (boxId == 0) {
            return;
        }

        auto& failure = failedBoxes_[boxId];
        ++failure.failedAttempts;

        std::cout << "[Collect] " << reason << " for box " << boxId
                  << " (attempt " << failure.failedAttempts
                  << "/" << MAX_FAILED_ATTEMPTS << ")\n";

        if (failure.failedAttempts >= MAX_FAILED_ATTEMPTS) {
            failure.ignoreUntilMs = now + FAILED_BOX_IGNORE_MS;
            std::cout << "[Collect] Ignoring box " << boxId
                      << " for " << (FAILED_BOX_IGNORE_MS / 1000) << "s\n";
        }
    }

    [[nodiscard]] bool shouldIgnoreAfterFailure(int32_t boxId, int64_t now) const {
        auto it = failedBoxes_.find(boxId);
        return it != failedBoxes_.end() && it->second.ignoreUntilMs > now;
    }

    void retryCurrentTarget(const GameSnapshot& snap, std::string_view reason) {
        const int32_t boxId = currentBoxId_;
        markBoxFailure(boxId, snap.timestampMs, reason);

        if (shouldIgnoreAfterFailure(boxId, snap.timestampMs)) {
            resetCurrentTarget(true);
            state_ = CollectState::Searching;
            return;
        }

        if (movement_) {
            movement_->release(name());
        }
        state_ = CollectState::Approaching;
        collectStartTime_ = 0;
        currentBoxAcquiredAtMs_ = snap.timestampMs;
        lastBoxProgressAtMs_ = snap.timestampMs;
        lastBoxDistance_ = std::numeric_limits<double>::infinity();
    }

    [[nodiscard]] bool shouldCollectBox(const BoxInfo& box, const GameSnapshot& snap) const {
        if (isBoxIgnored(box.id, snap.timestampMs)) {
            return false;
        }

        for (const auto& targetBox : config_.targetBoxes) {
            if (targetBox.type != box.type || !targetBox.enabled) {
                continue;
            }

            if (targetBox.type == static_cast<int32_t>(BoxType::GreenBox) &&
                config_.skipBootyIfNoKeys &&
                snap.hero.bootyKeys <= 0) {
                return false;
            }

            if (targetBox.type == static_cast<int32_t>(BoxType::CargoBox) &&
                config_.skipResourceIfCargoFull &&
                snap.hero.isCargoFull()) {
                return false;
            }

            return true;
        }
        return false;
    }

    [[nodiscard]] int32_t getBoxPriority(int32_t boxType) const {
        for (const auto& targetBox : config_.targetBoxes) {
            if (targetBox.type == boxType) {
                return targetBox.priority;
            }
        }
        return 0;
    }

    [[nodiscard]] std::optional<BoxSelection> findBestBox(const GameSnapshot& snap) const {
        const BoxInfo* bestBox = nullptr;
        int32_t bestId = 0;
        double bestScore = -1;

        Position heroPos(snap.hero.x, snap.hero.y);

        for (const auto& box : snap.entities.boxes) {
            if (!box.existsOnMap) continue;
            if (!shouldCollectBox(box, snap)) continue;

            Position boxPos(box.x, box.y);
            double dist = heroPos.distanceTo(boxPos);

            if (config_.maxCollectDistance > 0 && dist > config_.maxCollectDistance) {
                continue;
            }

            const int32_t priority = getBoxPriority(box.type);
            const double score = priority * 1000.0 - dist;

            if (score > bestScore) {
                bestScore = score;
                bestBox = &box;
                bestId = box.id;
            }
        }

        if (bestBox) {
            return BoxSelection{bestId, bestBox};
        }
        return std::nullopt;
    }

    [[nodiscard]] const BoxInfo* getCurrentBox(const GameSnapshot& snap) const {
        if (currentBoxId_ == 0) {
            return nullptr;
        }

        for (const auto& box : snap.entities.boxes) {
            if (box.id == currentBoxId_) {
                return &box;
            }
        }
        return nullptr;
    }

    void handleSearching(const GameSnapshot& snap) {
        auto result = findBestBox(snap);
        if (!result.has_value()) {
            return;
        }

        currentBoxId_ = result->id;
        beginTrackingBox(*result->box, snap);
        state_ = CollectState::Approaching;
        std::cout << "[Collect] Found box type=" << result->box->type
                  << " (id=" << currentBoxId_ << ")\n";
    }

    void handleApproaching(const GameSnapshot& snap) {
        const BoxInfo* box = getCurrentBox(snap);
        if (!box) {
            state_ = CollectState::Searching;
            resetCurrentTarget(true);
            return;
        }

        Position heroPos(snap.hero.x, snap.hero.y);
        Position boxPos(box->x, box->y);
        const double dist = heroPos.distanceTo(boxPos);

        if (lastBoxDistance_ - dist >= PROGRESS_DISTANCE_THRESHOLD) {
            lastBoxDistance_ = dist;
            lastBoxProgressAtMs_ = snap.timestampMs;
        } else if (dist < lastBoxDistance_) {
            lastBoxDistance_ = dist;
        }

        if (dist <= COLLECT_RANGE) {
            state_ = CollectState::Collecting;
            lastCollectTime_ = 0;
            lastBoxProgressAtMs_ = snap.timestampMs;
            lastBoxDistance_ = dist;
            return;
        }

        if ((currentBoxAcquiredAtMs_ > 0 &&
             snap.timestampMs - currentBoxAcquiredAtMs_ > APPROACH_MAX_AGE_MS) ||
            (lastBoxProgressAtMs_ > 0 &&
             snap.timestampMs - lastBoxProgressAtMs_ > APPROACH_STALL_TIMEOUT_MS)) {
            retryCurrentTarget(snap, "Approach stalled");
            return;
        }

        const auto now = snap.timestampMs;
        if (now - lastMoveTime_ >= config_.moveCooldownMs) {
            lastMoveTime_ = now;
            const Position approachPos = collectApproachPosition(*box);
            if (movement_) {
                movement_->move(name(), snap, approachPos, MoveIntent::Collect);
            } else {
                engine_->moveTo(static_cast<float>(approachPos.x), static_cast<float>(approachPos.y));
            }
        }
    }

    void handleCollecting(const GameSnapshot& snap) {
        const BoxInfo* box = getCurrentBox(snap);
        if (!box) {
            noteCollectionSuccess(currentBoxId_);
            state_ = CollectState::Searching;
            resetCurrentTarget(true);
            return;
        }

        Position heroPos(snap.hero.x, snap.hero.y);
        Position boxPos(box->x, box->y);
        const double dist = heroPos.distanceTo(boxPos);
        if (dist > COLLECT_RANGE) {
            state_ = CollectState::Approaching;
            lastBoxProgressAtMs_ = snap.timestampMs;
            lastBoxDistance_ = dist;
            return;
        }

        const auto now = snap.timestampMs;
        if (now - lastCollectTime_ >= config_.collectCooldownMs) {
            lastCollectTime_ = now;
            collectStartTime_ = now;
            currentCollectWaitUntilMs_ = now + collectRetryWaitMs(*box);

            engine_->collect(currentBoxId_);
            state_ = CollectState::Waiting;
            std::cout << "[Collect] Collecting box " << currentBoxId_ << "\n";
        }
    }

    void handleWaiting(const GameSnapshot& snap) {
        const BoxInfo* box = getCurrentBox(snap);

        if (!box) {
            noteCollectionSuccess(currentBoxId_);
            state_ = CollectState::Searching;
            resetCurrentTarget(true);
            return;
        }

        const auto now = snap.timestampMs;
        if (currentCollectWaitUntilMs_ > now) {
            return;
        }

        const int32_t boxId = currentBoxId_;
        markBoxFailure(boxId, now, "Collect failed");

        if (shouldIgnoreAfterFailure(boxId, now)) {
            resetCurrentTarget(true);
            state_ = CollectState::Searching;
            return;
        }

        lastCollectTime_ = now;
        collectStartTime_ = now;
        currentCollectWaitUntilMs_ = now + collectRetryWaitMs(*box);
        engine_->collect(boxId);
        std::cout << "[Collect] Retrying box " << boxId << " immediately\n";
    }
};

} // namespace dynamo
