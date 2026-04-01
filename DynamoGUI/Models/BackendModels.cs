namespace DynamoGUI.Models;

public sealed class MapEntity
{
    public int Id { get; set; }
    public int X { get; set; }
    public int Y { get; set; }
    public int Type { get; set; } // 0=npc, 1=enemy, 2=ally, 3=box, 4=portal, 5=station
    public string Name { get; set; } = string.Empty;
}

public sealed class BackendConnectRequest
{
    public string Username { get; set; } = string.Empty;
    public string Password { get; set; } = string.Empty;
    public string ServerId { get; set; } = "eu1";
    public string Language { get; set; } = "en";
}

public sealed class BackendCommandResult
{
    public int RequestId { get; set; }
    public int CommandType { get; set; }
    public bool Success { get; set; }
    public string Message { get; set; } = string.Empty;
}

public sealed class InventoryStatsSnapshot
{
    public long Plt { get; set; }
    public long Btc { get; set; }
    public long Experience { get; set; }
    public long Honor { get; set; }
    public long LaserRlx1 { get; set; }
    public long LaserGlx2 { get; set; }
    public long LaserBlx3 { get; set; }
    public long LaserWlx4 { get; set; }
    public long LaserGlx2As { get; set; }
    public long LaserMrs6X { get; set; }
    public long RocketKep410 { get; set; }
    public long RocketNc30 { get; set; }
    public long RocketTnc130 { get; set; }
    public long EnergyEe { get; set; }
    public long EnergyEn { get; set; }
    public long EnergyEg { get; set; }
    public long EnergyEm { get; set; }
}

public sealed class SessionStatsSnapshot
{
    public long RuntimeMs { get; set; }
    public InventoryStatsSnapshot Session { get; set; } = new();
    public InventoryStatsSnapshot Total { get; set; } = new();
}

public sealed class ResourceInventorySnapshot
{
    public long Cerium { get; set; }
    public long Mercury { get; set; }
    public long Erbium { get; set; }
    public long Piritid { get; set; }
    public long Darkonit { get; set; }
    public long Uranit { get; set; }
    public long Azurit { get; set; }
    public long Dungid { get; set; }
    public long Xureon { get; set; }
}

public sealed class BackendStatusSnapshot
{
    public string ConnectionState { get; set; } = "Disconnected";
    public string EngineState { get; set; } = "Offline";
    public string EngineError { get; set; } = string.Empty;
    public string ActiveProfile { get; set; } = "default";
    public string WorkingMap { get; set; } = "-";
    public string CurrentMap { get; set; } = "-";
    public string CurrentMode { get; set; } = "Idle";
    public string CurrentTask { get; set; } = "Awaiting backend";
    public string CurrentTarget { get; set; } = "-";
    public string TargetCategory { get; set; } = "-";
    public string SafetyReason { get; set; } = "-";
    public string CurrentLaser { get; set; } = "-";
    public string CurrentRocket { get; set; } = "-";
    public string HeroName { get; set; } = string.Empty;
    public bool BotRunning { get; set; }
    public bool BotPaused { get; set; }
    public bool SafetyActive { get; set; }
    public bool HeroMoving { get; set; }
    public bool HasTarget { get; set; }
    public int ThreatCount { get; set; }
    public long Btc { get; set; }
    public long Plt { get; set; }
    public long Honor { get; set; }
    public long Experience { get; set; }
    public int HpPercent { get; set; }
    public int ShieldPercent { get; set; }
    public int CargoPercent { get; set; }
    public int HeroX { get; set; }
    public int HeroY { get; set; }
    public int HeroTargetX { get; set; }
    public int HeroTargetY { get; set; }
    public int MapWidth { get; set; }
    public int MapHeight { get; set; }
    public int ActiveConfig { get; set; } = 1;
    public int NpcCount { get; set; }
    public int EnemyCount { get; set; }
    public int BoxCount { get; set; }
    public int PortalCount { get; set; }
    public int TargetX { get; set; }
    public int TargetY { get; set; }
    public int TargetHpPercent { get; set; }
    public int TargetShieldPercent { get; set; }
    public int TargetDistance { get; set; }
    public int DeathCount { get; set; }
    public string CombatState { get; set; } = "Idle";
    public string CombatDecision { get; set; } = "-";
    public string CombatMovement { get; set; } = "-";
    public string TravelState { get; set; } = "Idle";
    public string TravelDecision { get; set; } = "-";
    public string TravelDestination { get; set; } = "-";
    public string RoamingDecision { get; set; } = "Idle";
    public ResourceInventorySnapshot CurrentResources { get; set; } = new();
    public SessionStatsSnapshot Stats { get; set; } = new();
    public MapEntity[] MapEntities { get; set; } = [];
}

public sealed class ProfileListSnapshot
{
    public string ActiveProfile { get; set; } = "default";
    public string[] Profiles { get; set; } = [];
}
