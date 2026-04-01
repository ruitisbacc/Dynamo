#pragma once

#include <vector>
#include <string>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <functional>

namespace dynamo {

/**
 * @brief Queued action with timing and deduplication
 */
struct QueuedAction {
    std::vector<uint8_t> data;              // Packet data to send
    std::string name;                        // Action name for logging
    std::string dedupeKey;                   // Key for deduplication
    std::string throttleKey;                 // Key for throttling
    
    using TimePoint = std::chrono::steady_clock::time_point;
    TimePoint queueTime;                     // When action was queued
    TimePoint sendTime;                      // When to send (after delay)
    int throttleMs{0};                       // Minimum time between same throttle key
};

/**
 * @brief Action queue with throttling and deduplication
 * 
 * Prevents packet spam by:
 * - Deduplication: replacing previous action with same dedupeKey
 * - Throttling: enforcing minimum time between actions with same throttleKey
 * - Delays: scheduling actions for future send
 * 
 * Thread-safe for enqueueing from bot logic thread.
 */
class ActionQueue {
public:
    using SendCallback = std::function<void(const std::vector<uint8_t>&)>;
    
    ActionQueue() = default;
    
    /**
     * @brief Set callback for sending packets
     */
    void setSendCallback(SendCallback cb) { sendCallback_ = std::move(cb); }
    
    /**
     * @brief Queue an action
     * @param data Packet data
     * @param delayMs Delay before sending
     * @param name Action name
     * @param dedupeKey If set, replaces previous action with same key
     * @param throttleMs If set, enforces minimum interval between sends
     */
    void enqueue(std::vector<uint8_t> data, 
                 int delayMs = 0,
                 const std::string& name = "",
                 const std::string& dedupeKey = "",
                 int throttleMs = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::steady_clock::now();
        
        QueuedAction action;
        action.data = std::move(data);
        action.name = name;
        action.dedupeKey = dedupeKey;
        action.throttleKey = !dedupeKey.empty() ? dedupeKey : name;
        action.queueTime = now;
        action.sendTime = now + std::chrono::milliseconds(delayMs);
        action.throttleMs = throttleMs;
        
        // Deduplication: remove previous action with same key
        if (!dedupeKey.empty()) {
            for (auto it = queue_.begin(); it != queue_.end(); ) {
                if (it->dedupeKey == dedupeKey) {
                    it = queue_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        queue_.push_back(std::move(action));
    }
    
    /**
     * @brief Process queue and send ready actions
     * Call this regularly from engine update loop.
     */
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto now = std::chrono::steady_clock::now();
        
        for (auto it = queue_.begin(); it != queue_.end(); ) {
            // Not ready yet?
            if (now < it->sendTime) {
                ++it;
                continue;
            }
            
            // Check throttle
            if (it->throttleMs > 0 && !it->throttleKey.empty()) {
                auto lastSendIt = lastSendTimes_.find(it->throttleKey);
                if (lastSendIt != lastSendTimes_.end()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastSendIt->second).count();
                    if (elapsed < it->throttleMs) {
                        // Reschedule
                        it->sendTime = now + std::chrono::milliseconds(it->throttleMs - elapsed);
                        ++it;
                        continue;
                    }
                }
            }
            
            // Send action
            if (sendCallback_) {
                sendCallback_(it->data);
            }
            
            // Update throttle time
            if (!it->throttleKey.empty()) {
                lastSendTimes_[it->throttleKey] = now;
            }
            
            it = queue_.erase(it);
        }
    }
    
    /**
     * @brief Clear all queued actions
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
    
    /**
     * @brief Get number of pending actions
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    /**
     * @brief Clear throttle state for a key
     */
    void resetThrottle(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastSendTimes_.erase(key);
    }
    
    /**
     * @brief Clear all throttle state
     */
    void resetAllThrottles() {
        std::lock_guard<std::mutex> lock(mutex_);
        lastSendTimes_.clear();
    }
    
private:
    mutable std::mutex mutex_;
    std::deque<QueuedAction> queue_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastSendTimes_;
    SendCallback sendCallback_;
};

} // namespace dynamo
