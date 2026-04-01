/**
 * @brief Combat state machine states
 */
enum class CombatState {
    Searching,      // Looking for target
    Approaching,    // Moving towards target
    Attacking,      // Engaging target
    AwaitingKill,   // Waiting for target to die
    Cooldown        // Brief pause between kills
};

struct CombatTelemetry {
    CombatState state{CombatState::Searching};
    CombatMovementMode movementMode{CombatMovementMode::Adaptive};
    std::string movementDecision{"Idle"};
    std::string recoveryReason;
    std::string targetName;
    int32_t targetId{0};
    float targetDistance{0.0f};
    bool stuckRecoveryActive{false};
    int32_t stuckRecoveries{0};
    int64_t lastModeChangeMs{0};
    int64_t lastRecoveryMs{0};
    int64_t lastProgressMs{0};
};

struct CombatTargetSelection {
    const ShipInfo* ship{nullptr};
    const NpcTargetConfig* config{nullptr};
    double distance{0.0};
    double durabilityRatio{1.0};
    double score{0.0};
};

struct CombatTargetLockout {
    int32_t failures{0};
    int64_t untilMs{0};
};

struct CombatCollectSelection {
    const BoxInfo* box{nullptr};
    Position approachPosition;
};

struct CombatTargetMotion {
    Position current;
    Position destination;
    Position direction;
    Position anchor;
    double speed{0.0};
    double pathRemaining{0.0};
    double leadTime{0.0};
    bool moving{false};
};

struct OrbitSolution {
    Position point;
    Position anchor;
    double score{-std::numeric_limits<double>::infinity()};
    double rangeError{std::numeric_limits<double>::infinity()};
    double edgeClearance{0.0};
    bool clockwise{true};
    bool valid{false};
};

struct OrbitScoringContext {
    Position heroPos;
    Position targetPos;
    Position anchor;
    double desiredRange{0.0};
    double correctiveRadius{0.0};
    double currentAngle{0.0};
    std::optional<Position> collectApproachPoint;
};
