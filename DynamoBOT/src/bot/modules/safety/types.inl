/**
 * @brief Safety module states
 */
enum class SafetyState {
    Monitoring,         // Normal state, checking for threats
    Fleeing,            // Running away from danger
    AtPortal,           // Reached escape portal
    AwaitingTeleport,   // Teleport sent, waiting for map change
    Repairing,          // Low HP, waiting to repair
    ConfigSwitching     // Switching ship config
};

enum class RepairAnchorKind {
    None,
    Portal,
    Station
};

struct RepairAnchor {
    RepairAnchorKind kind{RepairAnchorKind::None};
    int32_t id{0};
    Position position;
    std::string label;
};

struct SafetySessionState {
    std::unordered_set<int32_t> rememberedAggressorIds;

    void reset() {
        rememberedAggressorIds.clear();
    }
};

struct SafetyTelemetry {
    SafetyState state{SafetyState::Monitoring};
    ThreatLevel threatLevel{ThreatLevel::None};
    std::string decision{"Monitoring"};
    std::string escapeMap;
    int32_t escapePortalId{0};
    int32_t visibleEnemies{0};
    int32_t closeEnemies{0};
    int32_t attackers{0};
    int32_t primaryThreatId{0};
    float hpPercent{100.0f};
    float primaryThreatDistance{0.0f};
    float fleeTargetX{0.0f};
    float fleeTargetY{0.0f};
    bool beingAttacked{false};
    bool adminSeenRecently{false};
    bool hostileApproachDetected{false};
    int32_t fleeRetargets{0};
    int64_t lastStateChangeMs{0};
    int64_t lastProgressMs{0};
};

[[nodiscard]] inline const char* repairAnchorKindName(RepairAnchorKind kind) {
    switch (kind) {
        case RepairAnchorKind::Portal: return "portal";
        case RepairAnchorKind::Station: return "station";
        case RepairAnchorKind::None:
        default: return "anchor";
    }
}
