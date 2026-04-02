using System.Collections.Generic;

namespace DynamoGUI.Models;

public enum BoxType
{
    BonusBox = 0,
    CargoBox = 1,
    EnergyBox = 2,
    GreenBox = 3,
}

public enum SafetyFleeMode
{
    None = 0,
    OnAttack = 1,
    OnEnemySeen = 2,
}

public enum EnrichmentMaterial
{
    Darkonit = 4,
    Uranit = 5,
    Azurit = 6,
    Dungid = 7,
    Xureon = 8,
}

public sealed class NpcVariantRule
{
    public bool Enabled { get; set; }
    public int AmmoType { get; set; } = 1;
    public int RocketType { get; set; }
    public int Range { get; set; } = 550;
    public bool FollowOnLowHp { get; set; }
    public int FollowOnLowHpPercent { get; set; } = 25;
    public bool IgnoreOwnership { get; set; }
}

public sealed class NpcProfileRule
{
    public string NpcName { get; set; } = string.Empty;
    public NpcVariantRule DefaultVariant { get; set; } = new();
    public NpcVariantRule HyperVariant { get; set; } = new();
    public NpcVariantRule UltraVariant { get; set; } = new();
}

public sealed class ConfigSlotSelection
{
    public int Roaming { get; set; } = 1;
    public int Flying { get; set; } = 2;
    public int Shooting { get; set; } = 1;
    public int Collect { get; set; } = 2;
}

public sealed class SafetyPolicy
{
    public int EmergencyHpPercent { get; set; } = 15;
    public int RepairHpPercent { get; set; } = 70;
    public int FullHpPercent { get; set; } = 100;
    public SafetyFleeMode FleeMode { get; set; } = SafetyFleeMode.OnEnemySeen;
}

public sealed class AdminDisconnectPolicy
{
    public bool Enabled { get; set; } = true;
    public int CooldownMinutes { get; set; } = 5;
}

public sealed class DeathDisconnectPolicy
{
    public bool Enabled { get; set; }
    public int DeathThreshold { get; set; } = 5;
    public int CooldownMinutes { get; set; } = 15;
}

public sealed class AutobuyConfig
{
    public bool LaserRlx1 { get; set; }
    public bool LaserGlx2 { get; set; }
    public bool LaserBlx3 { get; set; }
    public bool LaserGlx2As { get; set; }
    public bool LaserMrs6x { get; set; }
    public bool RocketKep410 { get; set; }
    public bool RocketNc30 { get; set; }
    public bool RocketTnc130 { get; set; }
}

public sealed class ResourceModuleSettings
{
    public bool Enabled { get; set; }
    public EnrichmentMaterial Material { get; set; } = EnrichmentMaterial.Uranit;
    public int Priority { get; set; } = 1;
}

public sealed class ResourceAutomationSettings
{
    public bool Enabled { get; set; }
    public bool SellWhenBlocked { get; set; }
    public int RefineIntervalSeconds { get; set; } = 120;
    public ResourceModuleSettings Lasers { get; set; } = new() { Material = EnrichmentMaterial.Darkonit, Priority = 3 };
    public ResourceModuleSettings Rockets { get; set; } = new() { Material = EnrichmentMaterial.Darkonit, Priority = 4 };
    public ResourceModuleSettings Shields { get; set; } = new() { Material = EnrichmentMaterial.Uranit, Priority = 2 };
    public ResourceModuleSettings Speed { get; set; } = new() { Material = EnrichmentMaterial.Uranit, Priority = 1 };
}

public sealed class BotProfile
{
    public int SchemaVersion { get; set; } = 2;
    public string Id { get; set; } = "default";
    public string DisplayName { get; set; } = "Default";
    public string WorkingMap { get; set; } = "R-1";
    public ConfigSlotSelection ConfigSlots { get; set; } = new();
    public bool Kill { get; set; } = true;
    public bool Collect { get; set; } = true;
    public bool CollectDuringCombat { get; set; } = true;
    public List<BoxType> BoxTypes { get; set; } = [];
    public List<string> AvoidMaps { get; set; } = [];
    public List<NpcProfileRule> NpcRules { get; set; } = [];
    public SafetyPolicy Safety { get; set; } = new();
    public AdminDisconnectPolicy AdminDisconnect { get; set; } = new();
    public DeathDisconnectPolicy DeathDisconnect { get; set; } = new();
    public ResourceAutomationSettings Resources { get; set; } = new();
    public AutobuyConfig Autobuy { get; set; } = new();
    public BotProfile DeepClone() => JsonDefaults.DeepClone(this);
}
