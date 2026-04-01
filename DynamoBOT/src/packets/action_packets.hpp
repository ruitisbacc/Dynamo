#pragma once

#include "network/kryo_serializer.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace dynamo {

// UserActionsPacket - player actions sent to server

// Action type constants
namespace ActionType {
    inline constexpr int32_t MOVE = 1;
    inline constexpr int32_t LOCK = 2;           // Lock target
    inline constexpr int32_t ATTACK = 3;
    inline constexpr int32_t STOP_ATTACK = 4;
    inline constexpr int32_t SWITCH_CONFI = 5;   // Switch configuration
    inline constexpr int32_t TELEPORT = 6;
    inline constexpr int32_t NBOMB = 7;          // Use N-bomb
    inline constexpr int32_t WSHIELD = 8;        // Use shield
    inline constexpr int32_t EMP = 9;
    inline constexpr int32_t INVIS = 10;         // Cloak
    inline constexpr int32_t LOGOUT = 11;
    inline constexpr int32_t SELECT_LASER = 12;  // Select laser ammo
    inline constexpr int32_t LOGOUT_CANCEL = 13;
    inline constexpr int32_t ENERGY_TRANSFER = 14;
    inline constexpr int32_t FAST_REPAIR = 15;
}

struct UserAction {
    int32_t actionId{0};
    std::string data;
    
    UserAction() = default;
    UserAction(int32_t id, const std::string& d = "") : actionId(id), data(d) {}
    
    void serialize(KryoBuffer& buf) const {
        // Alphabetical: actionId, data
        buf.writeKryoInt(actionId);
        buf.writeString(data);
    }
    
    void deserialize(KryoBuffer& buf) {
        actionId = buf.readKryoInt();
        data = buf.readString();
    }
    
    // Helper factories
    static UserAction move(float x, float y) {
        return UserAction(ActionType::MOVE, 
            std::to_string(static_cast<int>(x)) + "|" + 
            std::to_string(static_cast<int>(y)));
    }
    
    static UserAction lock(int32_t targetId) {
        return UserAction(ActionType::LOCK, std::to_string(targetId));
    }
    
    static UserAction attack() {
        return UserAction(ActionType::ATTACK);
    }
    
    static UserAction stopAttack() {
        return UserAction(ActionType::STOP_ATTACK);
    }
    
    static UserAction switchConfig(int32_t configIndex) {
        return UserAction(ActionType::SWITCH_CONFI, std::to_string(configIndex));
    }
    
    static UserAction teleport(int32_t portalId) {
        return UserAction(ActionType::TELEPORT, std::to_string(portalId));
    }
    
    static UserAction useNbomb() {
        return UserAction(ActionType::NBOMB);
    }
    
    static UserAction useShield() {
        return UserAction(ActionType::WSHIELD);
    }
    
    static UserAction useEmp() {
        return UserAction(ActionType::EMP);
    }
    
    static UserAction cloak() {
        return UserAction(ActionType::INVIS);
    }
    
    static UserAction logout() {
        return UserAction(ActionType::LOGOUT);
    }
    
    static UserAction cancelLogout() {
        return UserAction(ActionType::LOGOUT_CANCEL);
    }
    
    static UserAction selectLaser(int32_t ammoType) {
        return UserAction(ActionType::SELECT_LASER, std::to_string(ammoType));
    }
    
    static UserAction fastRepair() {
        return UserAction(ActionType::FAST_REPAIR);
    }
};

struct UserActionsPacket : public ISerializable {
    std::vector<UserAction> actions;
    
    UserActionsPacket() = default;
    
    // Convenience constructor for single action
    explicit UserActionsPacket(const UserAction& action) {
        actions.push_back(action);
    }
    
    // Convenience constructor for multiple actions
    explicit UserActionsPacket(std::vector<UserAction> acts) 
        : actions(std::move(acts)) {}
    
    void serialize(KryoBuffer& buf) const override {
        // UserAction[] class ID is 56 (wire 70)
        buf.writeVarInt(70, true);
        buf.writeVarInt(static_cast<int32_t>(actions.size()) + 1, true);
        for (const auto& action : actions) {
            // UserAction class ID is 55 (wire 69)
            buf.writeVarInt(69, true);
            action.serialize(buf);
        }
    }
    
    void deserialize(KryoBuffer& buf) override {
        int32_t arrayClassId = buf.readVarInt(true);
        if (arrayClassId == 0) {
            actions.clear();
            return;
        }
        
        int32_t size = buf.readVarInt(true);
        if (size > 0) size--;
        actions.resize(size);
        
        for (auto& action : actions) {
            int32_t elementClassId = buf.readVarInt(true);
            if (elementClassId != 0) {
                action.deserialize(buf);
            }
        }
    }
    
    // Helper methods
    void addMove(float x, float y) {
        actions.push_back(UserAction::move(x, y));
    }
    
    void addLock(int32_t targetId) {
        actions.push_back(UserAction::lock(targetId));
    }
    
    void addAttack() {
        actions.push_back(UserAction::attack());
    }
    
    void addStopAttack() {
        actions.push_back(UserAction::stopAttack());
    }
};

// CollectableCollectRequest - request to collect a box
struct CollectableCollectRequest : public ISerializable {
    int32_t collectableId{0};
    
    CollectableCollectRequest() = default;
    explicit CollectableCollectRequest(int32_t id) : collectableId(id) {}
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(collectableId);
    }
    
    void deserialize(KryoBuffer& buf) override {
        collectableId = buf.readKryoInt();
    }
};

// RocketShotRequest - fire rocket
struct RocketShotRequest : public ISerializable {
    int32_t targetId{0};
    
    RocketShotRequest() = default;
    explicit RocketShotRequest(int32_t id) : targetId(id) {}
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(targetId);
    }
    
    void deserialize(KryoBuffer& buf) override {
        targetId = buf.readKryoInt();
    }
};

// RocketSwitchRequest - switch rocket ammo type
struct RocketSwitchRequest : public ISerializable {
    int32_t rocketType{0};
    
    RocketSwitchRequest() = default;
    explicit RocketSwitchRequest(int32_t type) : rocketType(type) {}
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeKryoInt(rocketType);
    }
    
    void deserialize(KryoBuffer& buf) override {
        rocketType = buf.readKryoInt();
    }
};

// AutoRocketRequest - toggle auto-rocket
struct AutoRocketRequest : public ISerializable {
    bool enabled{false};
    
    AutoRocketRequest() = default;
    explicit AutoRocketRequest(bool e) : enabled(e) {}
    
    void serialize(KryoBuffer& buf) const override {
        buf.writeBool(enabled);
    }
    
    void deserialize(KryoBuffer& buf) override {
        enabled = buf.readBool();
    }
};

} // namespace dynamo
