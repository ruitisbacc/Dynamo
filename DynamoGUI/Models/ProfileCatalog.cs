using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace DynamoGUI.Models;

public sealed class IntOption
{
    public IntOption(int value, string label)
    {
        Value = value;
        Label = label;
    }

    public int Value { get; }
    public string Label { get; }

    public override string ToString() => Label;
}

public sealed class NpcCatalogEntry
{
    public NpcCatalogEntry(string name, params string[] spawnMaps)
    {
        Name = name;
        SpawnMaps = new ReadOnlyCollection<string>(spawnMaps);
    }

    public string Name { get; }
    public IReadOnlyList<string> SpawnMaps { get; }
    public string SpawnMapsText => SpawnMaps.Count == 0 ? "Unknown" : string.Join(", ", SpawnMaps);
}

public sealed class SafetyFleeModeOption
{
    public SafetyFleeModeOption(SafetyFleeMode value, string label)
    {
        Value = value;
        Label = label;
    }

    public SafetyFleeMode Value { get; }
    public string Label { get; }

    public override string ToString() => Label;
}

public sealed class EnrichmentMaterialOption
{
    public EnrichmentMaterialOption(EnrichmentMaterial value, string label)
    {
        Value = value;
        Label = label;
    }

    public EnrichmentMaterial Value { get; }
    public string Label { get; }

    public override string ToString() => Label;
}

public static class ProfileCatalog
{
    public static readonly IReadOnlyList<IntOption> SlotOptions =
    [
        new IntOption(1, "1"),
        new IntOption(2, "2"),
    ];

    public static readonly IReadOnlyList<IntOption> LaserAmmoOptions =
    [
        new IntOption(1, "RLX-1"),
        new IntOption(2, "GLX-2"),
        new IntOption(3, "BLX-3"),
        new IntOption(4, "WLX-4"),
        new IntOption(5, "GLX-2-AS"),
        new IntOption(6, "MRS-6X"),
    ];

    public static readonly IReadOnlyList<IntOption> RocketAmmoOptions =
    [
        new IntOption(0, "None"),
        new IntOption(1, "KEP-410"),
        new IntOption(2, "NC-30"),
        new IntOption(3, "TNC-130"),
    ];

    public static readonly IReadOnlyList<SafetyFleeModeOption> SafetyFleeModeOptions =
    [
        new SafetyFleeModeOption(SafetyFleeMode.None, "Disabled"),
        new SafetyFleeModeOption(SafetyFleeMode.OnAttack, "Flee on attack"),
        new SafetyFleeModeOption(SafetyFleeMode.OnEnemySeen, "Flee on enemy seen"),
    ];

    public static readonly IReadOnlyList<IntOption> ResourcePriorityOptions =
    [
        new IntOption(1, "1"),
        new IntOption(2, "2"),
        new IntOption(3, "3"),
        new IntOption(4, "4"),
    ];

    public static readonly IReadOnlyList<EnrichmentMaterialOption> LaserRocketEnrichmentOptions =
    [
        new EnrichmentMaterialOption(EnrichmentMaterial.Darkonit, "Darkonit"),
        new EnrichmentMaterialOption(EnrichmentMaterial.Uranit, "Uranit"),
        new EnrichmentMaterialOption(EnrichmentMaterial.Dungid, "Dungid"),
    ];

    public static readonly IReadOnlyList<EnrichmentMaterialOption> ShieldSpeedEnrichmentOptions =
    [
        new EnrichmentMaterialOption(EnrichmentMaterial.Uranit, "Uranit"),
        new EnrichmentMaterialOption(EnrichmentMaterial.Azurit, "Azurit"),
        new EnrichmentMaterialOption(EnrichmentMaterial.Xureon, "Xureon"),
    ];

    // Mirrors the backend map graph and NPC registry so the editor does not invent map ids locally.
    public static readonly IReadOnlyList<string> KnownMaps =
    [
        "R-1", "R-2", "R-3", "R-4", "R-5", "R-6", "R-7",
        "E-1", "E-2", "E-3", "E-4", "E-5", "E-6", "E-7",
        "U-1", "U-2", "U-3", "U-4", "U-5", "U-6", "U-7",
        "J-SO", "J-VO", "J-VS", "T-1", "G-1"
    ];

    // Mirrors DynamoBOT/src/config/bot_profile.hpp kNpcRegistry.
    public static readonly IReadOnlyList<NpcCatalogEntry> Npcs =
    [
        new NpcCatalogEntry("Hydro", "R-1", "R-2", "E-1", "E-2", "U-1", "U-2"),
        new NpcCatalogEntry("Jenta", "R-2", "R-3", "E-2", "E-3", "U-2", "U-3"),
        new NpcCatalogEntry("Mali", "R-3", "R-4", "E-3", "E-4", "U-3", "U-4"),
        new NpcCatalogEntry("Plarion", "R-3", "R-4", "E-3", "E-4", "U-3", "U-4"),
        new NpcCatalogEntry("Motron", "R-4", "R-5", "E-4", "E-5", "U-4", "U-5"),
        new NpcCatalogEntry("Xeon", "R-4", "R-5", "E-4", "E-5", "U-4", "U-5"),
        new NpcCatalogEntry("Bangoliour", "R-5", "R-6", "E-5", "E-6", "U-5", "U-6"),
        new NpcCatalogEntry("Zavientos", "R-6", "E-6", "U-6"),
        new NpcCatalogEntry("Magmius", "R-6", "E-6", "U-6"),
        new NpcCatalogEntry("Raider", "R-7", "E-7", "U-7", "J-SO", "J-VO", "J-VS"),
        new NpcCatalogEntry("Vortex", "J-SO", "J-VO", "J-VS"),
        new NpcCatalogEntry("Quattroid", "G-1"),
    ];

    public static IntOption FindSlotOption(int value) =>
        SlotOptions.FirstOrDefault(option => option.Value == value) ?? SlotOptions[0];

    public static IntOption FindLaserAmmoOption(int value) =>
        LaserAmmoOptions.FirstOrDefault(option => option.Value == value) ?? LaserAmmoOptions[0];

    public static IntOption FindRocketAmmoOption(int value) =>
        RocketAmmoOptions.FirstOrDefault(option => option.Value == value) ?? RocketAmmoOptions[0];

    public static SafetyFleeModeOption FindSafetyFleeModeOption(SafetyFleeMode value) =>
        SafetyFleeModeOptions.FirstOrDefault(option => option.Value == value) ?? SafetyFleeModeOptions[2];

    public static IntOption FindResourcePriorityOption(int value) =>
        ResourcePriorityOptions.FirstOrDefault(option => option.Value == value) ?? ResourcePriorityOptions[0];

    public static EnrichmentMaterialOption FindLaserRocketEnrichmentOption(EnrichmentMaterial value) =>
        LaserRocketEnrichmentOptions.FirstOrDefault(option => option.Value == value) ?? LaserRocketEnrichmentOptions[0];

    public static EnrichmentMaterialOption FindShieldSpeedEnrichmentOption(EnrichmentMaterial value) =>
        ShieldSpeedEnrichmentOptions.FirstOrDefault(option => option.Value == value) ?? ShieldSpeedEnrichmentOptions[0];

    public static NpcCatalogEntry? FindNpc(string name) =>
        Npcs.FirstOrDefault(entry => string.Equals(entry.Name, name, StringComparison.Ordinal));

    public static BotProfile CreateDefaultProfile()
    {
        var profile = new BotProfile();
        foreach (var npc in Npcs)
        {
            profile.NpcRules.Add(new NpcProfileRule
            {
                NpcName = npc.Name,
                DefaultVariant = new NpcVariantRule(),
                HyperVariant = new NpcVariantRule(),
                UltraVariant = new NpcVariantRule(),
            });
        }

        return profile;
    }
}
