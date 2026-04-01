    SafetyConfig config_;
    MapConfig mapConfig_;
    AdminConfig adminConfig_;
    std::shared_ptr<SafetySessionState> sessionState_;
    ThreatTracker threatTracker_;
    Navigation navigation_;
    SafetyState state_{SafetyState::Monitoring};

    int64_t lastMoveTime_{0};
    int64_t lastConfigSwitchTime_{0};
    int64_t lastProgressTime_{0};
    double lastFleeDistance_{std::numeric_limits<double>::max()};
    Position fleeTarget_;
    int32_t escapePortalId_{0};
    std::string escapePortalMap_;
    std::optional<RepairAnchor> escapeAnchor_;
    int32_t fleeRetargetCount_{0};
    std::optional<RepairAnchor> repairAnchor_;
    bool repairHoldIssued_{false};
    int64_t reviveGraceUntilMs_{0};
    std::string teleportFromMap_;      // Map name when teleport was sent
    int64_t teleportSentAtMs_{0};      // When teleport packet was sent
    bool unsafePortalLogged_{false};   // Avoid spamming "unsafe portal" log
    mutable std::mutex telemetryMutex_;
    SafetyTelemetry telemetry_;

    static constexpr int64_t FLEE_STALL_TIMEOUT_MS = 2800;
    static constexpr int64_t TELEPORT_TIMEOUT_MS = 8000;
    static constexpr int64_t REPAIR_STALL_TIMEOUT_MS = 4500;
    static constexpr int64_t REPAIR_HOLD_REISSUE_MS = 1200;
    static constexpr int64_t REVIVE_GRACE_PERIOD_MS = 5000;
    static constexpr double REPAIR_PROGRESS_DELTA = 60.0;
    static constexpr double REPAIR_PORTAL_ARRIVAL_RADIUS = 220.0;
    static constexpr double REPAIR_STATION_ARRIVAL_RADIUS = 260.0;
    static constexpr double ESCAPE_CORRIDOR_DANGER_RADIUS = 700.0;
    static constexpr double ESCAPE_ENEMY_PROXIMITY_RADIUS = 1100.0;

    struct EnemyFleeAssessment {
        bool active{false};
        bool visibleRememberedAggressor{false};
        bool adminOverride{false};
        std::optional<EnemyInfo> primaryThreat;
        std::vector<EnemyInfo> visibleEnemies;
    };
