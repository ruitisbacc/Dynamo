#pragma once

/**
 * @file scheduler.hpp
 * @brief Priority-based module scheduler
 * 
 * Evaluates all modules each tick and runs the one with highest priority.
 */

#include "bot/core/module.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <iostream>

namespace dynamo {

/**
 * @brief Priority-based module scheduler
 * 
 * Each tick, evaluates all modules' priorities and executes
 * the one with the highest value. Handles module transitions
 * (onStart/onStop) automatically.
 */
class Scheduler {
public:
    Scheduler() = default;
    ~Scheduler() = default;
    
    // Non-copyable
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    
    /**
     * @brief Add a module to the scheduler
     * 
     * Modules are evaluated in the order they were added when
     * priorities are equal (first added wins ties).
     */
    void addModule(std::unique_ptr<Module> module) {
        if (module) {
            std::cout << "[Scheduler] Adding module: " << module->name() << "\n";
            modules_.push_back(std::move(module));
        }
    }
    
    /**
     * @brief Remove all modules
     */
    void clear() {
        if (currentModule_) {
            currentModule_->onStop();
            currentModule_ = nullptr;
        }
        modules_.clear();
    }
    
    /**
     * @brief Evaluate all modules and tick the highest priority one
     * 
     * @param snap Current game state snapshot
     */
    void tick(const GameSnapshot& snap) {
        Module* bestModule = nullptr;
        int maxPriority = 0;  // Must be > 0 to run
        
        for (const auto& mod : modules_) {
            if (!mod->isEnabled()) {
                continue;
            }
            
            int priority = mod->getPriority(snap);
            if (priority <= 0) {
                continue;
            }
            
            // Higher priority wins; on tie, keep current module to reduce flapping
            if (priority > maxPriority || 
                (priority == maxPriority && mod.get() == currentModule_)) {
                maxPriority = priority;
                bestModule = mod.get();
            }
        }
        
        // Handle module transition
        if (bestModule != currentModule_) {
            if (currentModule_) {
                std::cout << "[Scheduler] Stopping module: " << currentModule_->name() << "\n";
                currentModule_->onStop();
            }
            
            currentModule_ = bestModule;
            
            if (currentModule_) {
                std::cout << "[Scheduler] Starting module: " << currentModule_->name() 
                          << " (priority=" << maxPriority << ")\n";
                currentModule_->onStart();
            }
        }
        
        // Execute current module
        if (currentModule_) {
            currentModule_->tick(snap);
        }
    }
    
    /**
     * @brief Get currently active module
     */
    [[nodiscard]] Module* currentModule() const noexcept { 
        return currentModule_; 
    }

    /**
     * @brief Stop the currently active module without removing registered modules
     *
     * Used when a controller-level workflow temporarily takes ownership away
     * from the scheduler. The next scheduler tick will start the best module
     * again through the normal onStart path.
     */
    void suspendCurrentModule() {
        if (!currentModule_) {
            return;
        }

        std::cout << "[Scheduler] Suspending module: " << currentModule_->name() << "\n";
        currentModule_->onStop();
        currentModule_ = nullptr;
    }
    
    /**
     * @brief Get name of currently active module
     */
    [[nodiscard]] std::string_view currentModuleName() const noexcept {
        return currentModule_ ? currentModule_->name() : "None";
    }
    
    /**
     * @brief Get number of registered modules
     */
    [[nodiscard]] std::size_t moduleCount() const noexcept {
        return modules_.size();
    }
    
    /**
     * @brief Find module by name
     */
    [[nodiscard]] Module* findModule(std::string_view name) const {
        for (const auto& mod : modules_) {
            if (mod->name() == name) {
                return mod.get();
            }
        }
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<Module>> modules_;
    Module* currentModule_{nullptr};
};

} // namespace dynamo
