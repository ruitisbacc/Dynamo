using Avalonia.Threading;
using DynamoGUI.Models;
using DynamoGUI.Services;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;

namespace DynamoGUI.ViewModels;

public sealed class MapSelectionItem : ViewModelBase
{
    private bool _isSelected;

    public MapSelectionItem(string name)
    {
        Name = name;
    }

    public string Name { get; }

    public bool IsSelected
    {
        get => _isSelected;
        set => SetProperty(ref _isSelected, value);
    }
}

public sealed class NpcVariantRuleViewModel : ViewModelBase
{
    private bool _enabled;
    private IntOption? _selectedLaserAmmo = ProfileCatalog.LaserAmmoOptions[0];
    private IntOption? _selectedRocketAmmo = ProfileCatalog.RocketAmmoOptions[0];
    private int _range = 550;
    private bool _followOnLowHp;
    private int _followOnLowHpPercent = 25;
    private bool _ignoreOwnership;

    public NpcVariantRuleViewModel(string npcName, string label)
    {
        NpcName = npcName;
        Label = label;
    }

    public const int MinRange = 200;
    public const int MaxRange = 800;
    public const int MinLowHpPercent = 1;
    public const int MaxLowHpPercent = 100;

    public string NpcName { get; }
    public string Label { get; }
    public string DialogTitle => $"{NpcName} {Label}";
    public IReadOnlyList<IntOption> LaserAmmoOptions => ProfileCatalog.LaserAmmoOptions;
    public IReadOnlyList<IntOption> RocketAmmoOptions => ProfileCatalog.RocketAmmoOptions;
    public string SettingsSummary =>
        $"Range {Range} | Low HP {(FollowOnLowHp ? $"follow <= {FollowOnLowHpPercent}%" : "stop")} | Ownership {(IgnoreOwnership ? "ignore" : "respect")}";

    public bool Enabled
    {
        get => _enabled;
        set => SetProperty(ref _enabled, value);
    }

    public IntOption? SelectedLaserAmmo
    {
        get => _selectedLaserAmmo;
        set => SetProperty(ref _selectedLaserAmmo, value);
    }

    public IntOption? SelectedRocketAmmo
    {
        get => _selectedRocketAmmo;
        set => SetProperty(ref _selectedRocketAmmo, value);
    }

    public int Range
    {
        get => _range;
        set
        {
            var clamped = Math.Clamp(value, MinRange, MaxRange);
            if (SetProperty(ref _range, clamped))
                OnPropertyChanged(nameof(SettingsSummary));
        }
    }

    public bool FollowOnLowHp
    {
        get => _followOnLowHp;
        set
        {
            if (SetProperty(ref _followOnLowHp, value))
            {
                OnPropertyChanged(nameof(IsFollowOnLowHpThresholdEnabled));
                OnPropertyChanged(nameof(SettingsSummary));
            }
        }
    }

    public int FollowOnLowHpPercent
    {
        get => _followOnLowHpPercent;
        set
        {
            var clamped = Math.Clamp(value, MinLowHpPercent, MaxLowHpPercent);
            if (SetProperty(ref _followOnLowHpPercent, clamped))
                OnPropertyChanged(nameof(SettingsSummary));
        }
    }

    public bool IsFollowOnLowHpThresholdEnabled => FollowOnLowHp;

    public bool IgnoreOwnership
    {
        get => _ignoreOwnership;
        set
        {
            if (SetProperty(ref _ignoreOwnership, value))
                OnPropertyChanged(nameof(SettingsSummary));
        }
    }

    public void LoadFrom(NpcVariantRule model)
    {
        Enabled = model.Enabled;
        SelectedLaserAmmo = ProfileCatalog.FindLaserAmmoOption(model.AmmoType);
        SelectedRocketAmmo = ProfileCatalog.FindRocketAmmoOption(model.RocketType);
        Range = model.Range;
        FollowOnLowHp = model.FollowOnLowHp;
        FollowOnLowHpPercent = model.FollowOnLowHpPercent;
        IgnoreOwnership = model.IgnoreOwnership;
    }

    public NpcVariantRule ToModel() => new()
    {
        Enabled = Enabled,
        AmmoType = SelectedLaserAmmo?.Value ?? 1,
        RocketType = SelectedRocketAmmo?.Value ?? 0,
        Range = Range,
        FollowOnLowHp = FollowOnLowHp,
        FollowOnLowHpPercent = FollowOnLowHpPercent,
        IgnoreOwnership = IgnoreOwnership,
    };
}

public sealed class NpcRuleViewModel : ViewModelBase
{
    public NpcRuleViewModel(NpcCatalogEntry catalogEntry)
    {
        CatalogEntry = catalogEntry;
        DefaultVariant = new NpcVariantRuleViewModel(catalogEntry.Name, "Normal");
        HyperVariant = new NpcVariantRuleViewModel(catalogEntry.Name, "Hyper");
        UltraVariant = new NpcVariantRuleViewModel(catalogEntry.Name, "Ultra");

        DefaultVariant.PropertyChanged += HandleVariantChanged;
        HyperVariant.PropertyChanged += HandleVariantChanged;
        UltraVariant.PropertyChanged += HandleVariantChanged;
    }

    public NpcCatalogEntry CatalogEntry { get; }
    public string NpcName => CatalogEntry.Name;
    public string SpawnMapsText => CatalogEntry.SpawnMapsText;
    public NpcVariantRuleViewModel DefaultVariant { get; }
    public NpcVariantRuleViewModel HyperVariant { get; }
    public NpcVariantRuleViewModel UltraVariant { get; }

    public int EnabledVariantCount =>
        (DefaultVariant.Enabled ? 1 : 0) +
        (HyperVariant.Enabled ? 1 : 0) +
        (UltraVariant.Enabled ? 1 : 0);

    public void LoadFrom(NpcProfileRule model)
    {
        DefaultVariant.LoadFrom(model.DefaultVariant);
        HyperVariant.LoadFrom(model.HyperVariant);
        UltraVariant.LoadFrom(model.UltraVariant);

        OnPropertyChanged(nameof(EnabledVariantCount));
    }

    public NpcProfileRule ToModel() => new()
    {
        NpcName = NpcName,
        DefaultVariant = DefaultVariant.ToModel(),
        HyperVariant = HyperVariant.ToModel(),
        UltraVariant = UltraVariant.ToModel(),
    };

    private void HandleVariantChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(NpcVariantRuleViewModel.Enabled))
            OnPropertyChanged(nameof(EnabledVariantCount));
    }
}

public sealed class ResourceModuleSettingViewModel : ViewModelBase
{
    private bool _enabled;
    private bool _parentEnabled;
    private EnrichmentMaterialOption? _selectedMaterial;
    private IntOption? _selectedPriority;

    public ResourceModuleSettingViewModel(
        string key,
        string label,
        IReadOnlyList<EnrichmentMaterialOption> materialOptions,
        int fallbackPriority)
    {
        Key = key;
        Label = label;
        MaterialOptions = materialOptions;
        FallbackPriority = fallbackPriority;
        _selectedMaterial = materialOptions.FirstOrDefault();
        _selectedPriority = ProfileCatalog.FindResourcePriorityOption(fallbackPriority);
    }

    public string Key { get; }
    public string Label { get; }
    public int FallbackPriority { get; }
    public IReadOnlyList<EnrichmentMaterialOption> MaterialOptions { get; }
    public IReadOnlyList<IntOption> PriorityOptions => ProfileCatalog.ResourcePriorityOptions;

    public bool Enabled
    {
        get => _enabled;
        set
        {
            if (SetProperty(ref _enabled, value))
                OnPropertyChanged(nameof(IsEditable));
        }
    }

    public EnrichmentMaterialOption? SelectedMaterial
    {
        get => _selectedMaterial;
        set => SetProperty(ref _selectedMaterial, value);
    }

    public IntOption? SelectedPriority
    {
        get => _selectedPriority;
        set => SetProperty(ref _selectedPriority, value);
    }

    public bool IsEditable => _parentEnabled && Enabled;

    public void SetParentEnabled(bool enabled)
    {
        if (_parentEnabled == enabled)
        {
            return;
        }

        _parentEnabled = enabled;
        OnPropertyChanged(nameof(IsEditable));
    }

    public void LoadFrom(ResourceModuleSettings settings)
    {
        Enabled = settings.Enabled;
        SelectedPriority = ProfileCatalog.FindResourcePriorityOption(settings.Priority);
        SelectedMaterial = MaterialOptions.FirstOrDefault(option => option.Value == settings.Material)
            ?? MaterialOptions.FirstOrDefault();
    }

    public ResourceModuleSettings ToModel() => new()
    {
        Enabled = Enabled,
        Material = SelectedMaterial?.Value ?? MaterialOptions.First().Value,
        Priority = SelectedPriority?.Value ?? FallbackPriority,
    };
}

public sealed class ProfileEditorViewModel : ViewModelBase, IDisposable
{
    private readonly IpcClient _ipc;
    private bool _disposed;
    private bool _isConnected;
    private bool _suppressAutoLoad;

    private string? _selectedProfileId;
    private string _statusText = "Connect to backend to manage profiles.";

    // Profile fields
    private string _profileId = "default";
    private string _displayName = "Default";
    private string? _selectedWorkingMap;
    private IntOption? _selectedRoamingSlot = ProfileCatalog.SlotOptions[0];
    private IntOption? _selectedFlyingSlot = ProfileCatalog.SlotOptions[1];
    private IntOption? _selectedShootingSlot = ProfileCatalog.SlotOptions[0];
    private bool _kill = true;
    private bool _collect = true;
    private bool _collectDuringCombat = true;
    private bool _bonusBoxEnabled;
    private bool _cargoBoxEnabled;
    private bool _greenBoxEnabled;
    private string _emergencyHpPercent = "15";
    private string _repairHpPercent = "70";
    private string _fullHpPercent = "100";
    private SafetyFleeModeOption? _selectedSafetyFleeMode = ProfileCatalog.SafetyFleeModeOptions[2];
    private bool _adminDisconnectEnabled = true;
    private string _adminCooldownMinutes = "5";
    private bool _deathDisconnectEnabled;
    private string _deathThreshold = "5";
    private string _deathCooldownMinutes = "15";
    private bool _resourceAutomationEnabled;
    private bool _sellCargoWhenBlocked;
    private string _refineIntervalSeconds = "120";

    // Autobuy
    private bool _autobuyLaserRlx1;
    private bool _autobuyLaserGlx2;
    private bool _autobuyLaserBlx3;
    private bool _autobuyLaserGlx2As;
    private bool _autobuyLaserMrs6x;
    private bool _autobuyRocketKep410;
    private bool _autobuyRocketNc30;
    private bool _autobuyRocketTnc130;
    private readonly ResourceModuleSettingViewModel _speedResourceModule;
    private readonly ResourceModuleSettingViewModel _shieldResourceModule;
    private readonly ResourceModuleSettingViewModel _laserResourceModule;
    private readonly ResourceModuleSettingViewModel _rocketResourceModule;

    public ProfileEditorViewModel(IpcClient ipc)
    {
        _ipc = ipc ?? throw new ArgumentNullException(nameof(ipc));

        _speedResourceModule = new ResourceModuleSettingViewModel(
            "speed",
            "Speed",
            ProfileCatalog.ShieldSpeedEnrichmentOptions,
            fallbackPriority: 1);
        _shieldResourceModule = new ResourceModuleSettingViewModel(
            "shields",
            "Shields",
            ProfileCatalog.ShieldSpeedEnrichmentOptions,
            fallbackPriority: 2);
        _laserResourceModule = new ResourceModuleSettingViewModel(
            "lasers",
            "Lasers",
            ProfileCatalog.LaserRocketEnrichmentOptions,
            fallbackPriority: 3);
        _rocketResourceModule = new ResourceModuleSettingViewModel(
            "rockets",
            "Rockets",
            ProfileCatalog.LaserRocketEnrichmentOptions,
            fallbackPriority: 4);

        foreach (var map in ProfileCatalog.KnownMaps)
        {
            var item = new MapSelectionItem(map);
            item.PropertyChanged += HandleAvoidMapChanged;
            AvoidMaps.Add(item);
        }

        ResourceModules.Add(_speedResourceModule);
        ResourceModules.Add(_shieldResourceModule);
        ResourceModules.Add(_laserResourceModule);
        ResourceModules.Add(_rocketResourceModule);

        _selectedWorkingMap = ProfileCatalog.KnownMaps[0];
        RebuildNpcRules([]);
        SyncResourceModuleAvailability();

        _ipc.OnConnected += HandleConnected;
        _ipc.OnDisconnected += HandleDisconnected;
        _ipc.OnProfilesSnapshot += HandleProfilesSnapshot;
        _ipc.OnProfileDocument += HandleProfileDocument;

        // IPC may already be connected (LoginWindow connects before MainWindow is created)
        if (_ipc.IsConnected)
        {
            _isConnected = true;
            _ = InitialLoadAsync();
        }
    }

    // Collections
    public ObservableCollection<string> Profiles { get; } = new();
    public ObservableCollection<MapSelectionItem> AvoidMaps { get; } = new();
    public ObservableCollection<NpcRuleViewModel> NpcRules { get; } = new();
    public ObservableCollection<ResourceModuleSettingViewModel> ResourceModules { get; } = new();

    // Static option lists
    public IReadOnlyList<string> KnownMaps => ProfileCatalog.KnownMaps;
    public IReadOnlyList<IntOption> SlotOptions => ProfileCatalog.SlotOptions;
    public IReadOnlyList<SafetyFleeModeOption> SafetyFleeModeOptions => ProfileCatalog.SafetyFleeModeOptions;

    // Connection
    public bool IsConnected
    {
        get => _isConnected;
        private set
        {
            if (SetProperty(ref _isConnected, value))
                OnPropertyChanged(nameof(CanSave));
        }
    }

    // Profile selector — auto-loads on change
    public string? SelectedProfileId
    {
        get => _selectedProfileId;
        set
        {
            if (SetProperty(ref _selectedProfileId, value) && !_suppressAutoLoad && IsConnected)
                _ = AutoLoadProfileAsync(value);
        }
    }

    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }

    public bool CanSave => IsConnected && !string.IsNullOrWhiteSpace(ProfileId);

    // ══════ Profile Field Properties ══════

    public string ProfileId
    {
        get => _profileId;
        set
        {
            if (SetProperty(ref _profileId, value))
                OnPropertyChanged(nameof(CanSave));
        }
    }

    public string DisplayName
    {
        get => _displayName;
        set => SetProperty(ref _displayName, value);
    }

    public string? SelectedWorkingMap
    {
        get => _selectedWorkingMap;
        set => SetProperty(ref _selectedWorkingMap, value);
    }

    public IntOption? SelectedRoamingSlot
    {
        get => _selectedRoamingSlot;
        set => SetProperty(ref _selectedRoamingSlot, value);
    }

    public IntOption? SelectedFlyingSlot
    {
        get => _selectedFlyingSlot;
        set => SetProperty(ref _selectedFlyingSlot, value);
    }

    public IntOption? SelectedShootingSlot
    {
        get => _selectedShootingSlot;
        set => SetProperty(ref _selectedShootingSlot, value);
    }

    public bool Kill
    {
        get => _kill;
        set
        {
            if (SetProperty(ref _kill, value))
            {
                if (!value || !Collect) SetCollectDuringCombatSilent(false);
                OnPropertyChanged(nameof(IsCollectDuringCombatEnabled));
            }
        }
    }

    public bool Collect
    {
        get => _collect;
        set
        {
            if (SetProperty(ref _collect, value))
            {
                if (!Kill || !value) SetCollectDuringCombatSilent(false);
                OnPropertyChanged(nameof(IsCollectDuringCombatEnabled));
            }
        }
    }

    public bool CollectDuringCombat
    {
        get => _collectDuringCombat;
        set => SetProperty(ref _collectDuringCombat, value);
    }

    public bool IsCollectDuringCombatEnabled => Kill && Collect;

    public bool BonusBoxEnabled { get => _bonusBoxEnabled; set => SetProperty(ref _bonusBoxEnabled, value); }
    public bool CargoBoxEnabled { get => _cargoBoxEnabled; set => SetProperty(ref _cargoBoxEnabled, value); }
    public bool GreenBoxEnabled { get => _greenBoxEnabled; set => SetProperty(ref _greenBoxEnabled, value); }

    public string EmergencyHpPercent
    {
        get => _emergencyHpPercent;
        set => ClampedIntSet(ref _emergencyHpPercent, value, 1, 100, nameof(EmergencyHpPercent));
    }

    public string RepairHpPercent
    {
        get => _repairHpPercent;
        set => ClampedIntSet(ref _repairHpPercent, value, 1, 100, nameof(RepairHpPercent));
    }

    public string FullHpPercent
    {
        get => _fullHpPercent;
        set => ClampedIntSet(ref _fullHpPercent, value, 1, 100, nameof(FullHpPercent));
    }

    public SafetyFleeModeOption? SelectedSafetyFleeMode
    {
        get => _selectedSafetyFleeMode;
        set => SetProperty(ref _selectedSafetyFleeMode, value);
    }

    public bool AdminDisconnectEnabled
    {
        get => _adminDisconnectEnabled;
        set
        {
            if (SetProperty(ref _adminDisconnectEnabled, value))
                OnPropertyChanged(nameof(AreAdminFieldsEnabled));
        }
    }

    public bool AreAdminFieldsEnabled => AdminDisconnectEnabled;

    public string AdminCooldownMinutes
    {
        get => _adminCooldownMinutes;
        set => ClampedIntSet(ref _adminCooldownMinutes, value, 0, 180, nameof(AdminCooldownMinutes));
    }

    public bool DeathDisconnectEnabled
    {
        get => _deathDisconnectEnabled;
        set
        {
            if (SetProperty(ref _deathDisconnectEnabled, value))
                OnPropertyChanged(nameof(AreDeathFieldsEnabled));
        }
    }

    public bool AreDeathFieldsEnabled => DeathDisconnectEnabled;
    public string DeathThreshold
    {
        get => _deathThreshold;
        set => ClampedIntSet(ref _deathThreshold, value, 1, 50, nameof(DeathThreshold));
    }

    public string DeathCooldownMinutes
    {
        get => _deathCooldownMinutes;
        set => ClampedIntSet(ref _deathCooldownMinutes, value, 0, 180, nameof(DeathCooldownMinutes));
    }

    public bool ResourceAutomationEnabled
    {
        get => _resourceAutomationEnabled;
        set
        {
            if (SetProperty(ref _resourceAutomationEnabled, value))
            {
                OnPropertyChanged(nameof(IsSellCargoEnabled));
                OnPropertyChanged(nameof(IsRefineIntervalEnabled));
                SyncResourceModuleAvailability();
            }
        }
    }

    public bool SellCargoWhenBlocked
    {
        get => _sellCargoWhenBlocked;
        set => SetProperty(ref _sellCargoWhenBlocked, value);
    }

    public bool IsSellCargoEnabled => ResourceAutomationEnabled;

    public string RefineIntervalSeconds
    {
        get => _refineIntervalSeconds;
        set => ClampedIntSet(ref _refineIntervalSeconds, value, 30, 600, nameof(RefineIntervalSeconds));
    }

    public bool IsRefineIntervalEnabled => ResourceAutomationEnabled;

    // Autobuy
    public bool AutobuyLaserRlx1 { get => _autobuyLaserRlx1; set => SetProperty(ref _autobuyLaserRlx1, value); }
    public bool AutobuyLaserGlx2 { get => _autobuyLaserGlx2; set => SetProperty(ref _autobuyLaserGlx2, value); }
    public bool AutobuyLaserBlx3 { get => _autobuyLaserBlx3; set => SetProperty(ref _autobuyLaserBlx3, value); }
    public bool AutobuyLaserGlx2As { get => _autobuyLaserGlx2As; set => SetProperty(ref _autobuyLaserGlx2As, value); }
    public bool AutobuyLaserMrs6x { get => _autobuyLaserMrs6x; set => SetProperty(ref _autobuyLaserMrs6x, value); }
    public bool AutobuyRocketKep410 { get => _autobuyRocketKep410; set => SetProperty(ref _autobuyRocketKep410, value); }
    public bool AutobuyRocketNc30 { get => _autobuyRocketNc30; set => SetProperty(ref _autobuyRocketNc30, value); }
    public bool AutobuyRocketTnc130 { get => _autobuyRocketTnc130; set => SetProperty(ref _autobuyRocketTnc130, value); }

    // ══════ Commands ══════

    /// <summary>
    /// Creates a new profile. Called from code-behind after the name dialog.
    /// </summary>
    public void CreateNew(string name)
    {
        var id = SanitizeId(name);
        if (string.IsNullOrWhiteSpace(id))
        {
            StatusText = "Invalid profile name.";
            return;
        }

        var defaultProfile = ProfileCatalog.CreateDefaultProfile();
        defaultProfile.Id = id;
        defaultProfile.DisplayName = name.Trim();
        LoadFromProfile(defaultProfile);
        StatusText = $"New profile '{id}' — edit and save.";
    }

    public async Task SaveAsync()
    {
        if (!IsConnected)
        {
            StatusText = "Not connected.";
            return;
        }

        var profile = BuildProfile();
        var errors = ValidateProfile(profile);
        if (errors.Count > 0)
        {
            StatusText = "⚠ " + string.Join(" · ", errors);
            return;
        }

        StatusText = $"Saving '{profile.Id}'...";
        var result = await _ipc.SaveProfileDocumentAsync(profile);
        if (result.Success)
        {
            StatusText = $"✓ Saved '{profile.Id}'";
            _suppressAutoLoad = true;
            if (!Profiles.Contains(profile.Id))
                Profiles.Add(profile.Id);
            SelectedProfileId = profile.Id;
            _suppressAutoLoad = false;
            await _ipc.RequestProfilesAsync();
        }
        else
        {
            StatusText = string.IsNullOrWhiteSpace(result.Message)
                ? $"Failed to save '{profile.Id}'."
                : result.Message;
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        _ipc.OnConnected -= HandleConnected;
        _ipc.OnDisconnected -= HandleDisconnected;
        _ipc.OnProfilesSnapshot -= HandleProfilesSnapshot;
        _ipc.OnProfileDocument -= HandleProfileDocument;

        foreach (var map in AvoidMaps)
            map.PropertyChanged -= HandleAvoidMapChanged;
    }

    // ══════ IPC Handlers ══════

    private void HandleConnected()
    {
        Dispatcher.UIThread.Post(() =>
        {
            IsConnected = true;
            StatusText = "Connected. Loading profiles...";
            _ = InitialLoadAsync();
        });
    }

    private async Task InitialLoadAsync()
    {
        await _ipc.RequestProfilesAsync();
        await _ipc.RequestActiveProfileAsync();
    }

    private void HandleDisconnected()
    {
        Dispatcher.UIThread.Post(() =>
        {
            IsConnected = false;
            StatusText = "Disconnected.";
        });
    }

    private void HandleProfilesSnapshot(ProfileListSnapshot snapshot)
    {
        Dispatcher.UIThread.Post(() =>
        {
            var desiredSelection = !string.IsNullOrWhiteSpace(snapshot.ActiveProfile)
                ? snapshot.ActiveProfile
                : SelectedProfileId;

            _suppressAutoLoad = true;
            Profiles.Clear();
            foreach (var p in snapshot.Profiles
                         .Where(p => !string.IsNullOrWhiteSpace(p))
                         .Distinct(StringComparer.Ordinal)
                         .OrderBy(p => p, StringComparer.Ordinal))
            {
                Profiles.Add(p);
            }

            if (!string.IsNullOrWhiteSpace(snapshot.ActiveProfile) && !Profiles.Contains(snapshot.ActiveProfile))
                Profiles.Add(snapshot.ActiveProfile);

            desiredSelection = !string.IsNullOrWhiteSpace(desiredSelection) && Profiles.Contains(desiredSelection)
                ? desiredSelection
                : Profiles.FirstOrDefault();

            _selectedProfileId = null;
            OnPropertyChanged(nameof(SelectedProfileId));
            _selectedProfileId = desiredSelection;
            OnPropertyChanged(nameof(SelectedProfileId));

            _suppressAutoLoad = false;
        });
    }

    private void HandleProfileDocument(BotProfile profile)
    {
        Dispatcher.UIThread.Post(() =>
        {
            LoadFromProfile(profile);
            StatusText = $"Loaded '{profile.Id}'.";
        });
    }

    private async Task AutoLoadProfileAsync(string? profileId)
    {
        if (string.IsNullOrWhiteSpace(profileId)) return;

        StatusText = $"Loading '{profileId}'...";
        var result = await _ipc.LoadProfileAsync(profileId);
        if (!result.Success)
        {
            StatusText = string.IsNullOrWhiteSpace(result.Message)
                ? $"Failed to load '{profileId}'."
                : result.Message;
        }
        // Profile document arrives via HandleProfileDocument
    }

    // ══════ Internal ══════

    private void LoadFromProfile(BotProfile profile)
    {
        var normalized = NormalizeProfile(profile);
        _suppressAutoLoad = true;

        ProfileId = normalized.Id;
        DisplayName = normalized.DisplayName;
        SelectedWorkingMap = KnownMaps.Contains(normalized.WorkingMap)
            ? normalized.WorkingMap
            : KnownMaps[0];
        SelectedRoamingSlot = ProfileCatalog.FindSlotOption(normalized.ConfigSlots.Roaming);
        SelectedFlyingSlot = ProfileCatalog.FindSlotOption(normalized.ConfigSlots.Flying);
        SelectedShootingSlot = ProfileCatalog.FindSlotOption(normalized.ConfigSlots.Shooting);
        Kill = normalized.Kill;
        Collect = normalized.Collect;
        CollectDuringCombat = normalized.CollectDuringCombat;

        BonusBoxEnabled = normalized.BoxTypes.Contains(BoxType.BonusBox);
        CargoBoxEnabled = normalized.BoxTypes.Contains(BoxType.CargoBox);
        GreenBoxEnabled = normalized.BoxTypes.Contains(BoxType.GreenBox);

        EmergencyHpPercent = normalized.Safety.EmergencyHpPercent.ToString();
        RepairHpPercent = normalized.Safety.RepairHpPercent.ToString();
        FullHpPercent = normalized.Safety.FullHpPercent.ToString();
        SelectedSafetyFleeMode = ProfileCatalog.FindSafetyFleeModeOption(normalized.Safety.FleeMode);

        AdminDisconnectEnabled = normalized.AdminDisconnect.Enabled;
        AdminCooldownMinutes = normalized.AdminDisconnect.CooldownMinutes.ToString();
        DeathDisconnectEnabled = normalized.DeathDisconnect.Enabled;
        DeathThreshold = normalized.DeathDisconnect.DeathThreshold.ToString();
        DeathCooldownMinutes = normalized.DeathDisconnect.CooldownMinutes.ToString();
        ResourceAutomationEnabled = normalized.Resources.Enabled;
        SellCargoWhenBlocked = normalized.Resources.SellWhenBlocked;
        RefineIntervalSeconds = normalized.Resources.RefineIntervalSeconds.ToString();
        _speedResourceModule.LoadFrom(normalized.Resources.Speed);
        _shieldResourceModule.LoadFrom(normalized.Resources.Shields);
        _laserResourceModule.LoadFrom(normalized.Resources.Lasers);
        _rocketResourceModule.LoadFrom(normalized.Resources.Rockets);
        SyncResourceModuleAvailability();
        var ab = normalized.Autobuy ?? new AutobuyConfig();
        AutobuyLaserRlx1 = ab.LaserRlx1;
        AutobuyLaserGlx2 = ab.LaserGlx2;
        AutobuyLaserBlx3 = ab.LaserBlx3;
        AutobuyLaserGlx2As = ab.LaserGlx2As;
        AutobuyLaserMrs6x = ab.LaserMrs6x;
        AutobuyRocketKep410 = ab.RocketKep410;
        AutobuyRocketNc30 = ab.RocketNc30;
        AutobuyRocketTnc130 = ab.RocketTnc130;

        var blocked = new HashSet<string>(normalized.AvoidMaps, StringComparer.Ordinal);
        foreach (var map in AvoidMaps)
            map.IsSelected = blocked.Contains(map.Name);

        RebuildNpcRules(normalized.NpcRules);

        if (!Profiles.Contains(normalized.Id))
            Profiles.Add(normalized.Id);
        SelectedProfileId = normalized.Id;

        _suppressAutoLoad = false;
    }

    private static BotProfile NormalizeProfile(BotProfile profile)
    {
        var normalized = profile.DeepClone();
        normalized.SchemaVersion = Math.Max(normalized.SchemaVersion, 2);
        normalized.NpcRules ??= [];
        normalized.BoxTypes ??= [];
        normalized.AvoidMaps ??= [];
        normalized.Resources = NormalizeResourceAutomation(normalized.Resources);

        var rulesByName = normalized.NpcRules
            .Where(r => !string.IsNullOrWhiteSpace(r.NpcName))
            .GroupBy(r => r.NpcName, StringComparer.Ordinal)
            .ToDictionary(g => g.Key, g => g.First(), StringComparer.Ordinal);

        normalized.NpcRules =
        [
            .. ProfileCatalog.Npcs.Select(entry =>
            {
                if (rulesByName.TryGetValue(entry.Name, out var existing))
                {
                    return existing;
                }
                return new NpcProfileRule
                {
                    NpcName = entry.Name,
                    DefaultVariant = new NpcVariantRule(),
                    HyperVariant = new NpcVariantRule(),
                    UltraVariant = new NpcVariantRule(),
                };
            })
        ];
        normalized.BoxTypes =
        [
            .. normalized.BoxTypes
                .Where(type => type != BoxType.EnergyBox)
                .Distinct()
        ];

        return normalized;
    }

    private void RebuildNpcRules(IEnumerable<NpcProfileRule> rules)
    {
        NpcRules.Clear();
        var rulesByName = rules.ToDictionary(r => r.NpcName, StringComparer.Ordinal);
        foreach (var entry in ProfileCatalog.Npcs)
        {
            var vm = new NpcRuleViewModel(entry);
            if (rulesByName.TryGetValue(entry.Name, out var rule))
                vm.LoadFrom(rule);
            else
                vm.LoadFrom(new NpcProfileRule
                {
                    NpcName = entry.Name,
                    DefaultVariant = new NpcVariantRule(),
                    HyperVariant = new NpcVariantRule(),
                    UltraVariant = new NpcVariantRule(),
                });
            NpcRules.Add(vm);
        }
    }

    private BotProfile BuildProfile()
    {
        return new BotProfile
        {
            SchemaVersion = 2,
            Id = (ProfileId ?? string.Empty).Trim(),
            DisplayName = (DisplayName ?? string.Empty).Trim(),
            WorkingMap = SelectedWorkingMap ?? "R-1",
            ConfigSlots = new ConfigSlotSelection
            {
                Roaming = SelectedRoamingSlot?.Value ?? 1,
                Flying = SelectedFlyingSlot?.Value ?? 2,
                Shooting = SelectedShootingSlot?.Value ?? 1,
            },
            Kill = Kill,
            Collect = Collect,
            CollectDuringCombat = CollectDuringCombat && Kill && Collect,
            BoxTypes = [.. GetSelectedBoxTypes()],
            AvoidMaps = [.. AvoidMaps.Where(m => m.IsSelected).Select(m => m.Name)],
            NpcRules = [.. NpcRules.Select(r => r.ToModel())],
            Safety = new SafetyPolicy
            {
                EmergencyHpPercent = ParseInt(EmergencyHpPercent, 15),
                RepairHpPercent = ParseInt(RepairHpPercent, 70),
                FullHpPercent = ParseInt(FullHpPercent, 100),
                FleeMode = SelectedSafetyFleeMode?.Value ?? SafetyFleeMode.OnEnemySeen,
            },
            AdminDisconnect = new AdminDisconnectPolicy
            {
                Enabled = AdminDisconnectEnabled,
                CooldownMinutes = ParseInt(AdminCooldownMinutes, 5),
            },
            DeathDisconnect = new DeathDisconnectPolicy
            {
                Enabled = DeathDisconnectEnabled,
                DeathThreshold = ParseInt(DeathThreshold, 5),
                CooldownMinutes = ParseInt(DeathCooldownMinutes, 15),
            },
            Resources = BuildResourceAutomation(),
            Autobuy = new AutobuyConfig
            {
                LaserRlx1 = AutobuyLaserRlx1,
                LaserGlx2 = AutobuyLaserGlx2,
                LaserBlx3 = AutobuyLaserBlx3,
                LaserGlx2As = AutobuyLaserGlx2As,
                LaserMrs6x = AutobuyLaserMrs6x,
                RocketKep410 = AutobuyRocketKep410,
                RocketNc30 = AutobuyRocketNc30,
                RocketTnc130 = AutobuyRocketTnc130,
            },
        };
    }

    private List<BoxType> GetSelectedBoxTypes()
    {
        var result = new List<BoxType>(3);
        if (BonusBoxEnabled) result.Add(BoxType.BonusBox);
        if (CargoBoxEnabled) result.Add(BoxType.CargoBox);
        if (GreenBoxEnabled) result.Add(BoxType.GreenBox);
        return result;
    }

    private ResourceAutomationSettings BuildResourceAutomation() =>
        NormalizeResourceAutomation(new ResourceAutomationSettings
        {
            Enabled = ResourceAutomationEnabled,
            SellWhenBlocked = SellCargoWhenBlocked,
            RefineIntervalSeconds = ParseInt(RefineIntervalSeconds, 120),
            Speed = _speedResourceModule.ToModel(),
            Shields = _shieldResourceModule.ToModel(),
            Lasers = _laserResourceModule.ToModel(),
            Rockets = _rocketResourceModule.ToModel(),
        });

    private static List<string> ValidateProfile(BotProfile p)
    {
        var e = new List<string>();
        if (string.IsNullOrWhiteSpace(p.Id)) e.Add("Profile ID is required.");
        if (string.IsNullOrWhiteSpace(p.WorkingMap)) e.Add("Working map is required.");
        if (!p.Kill && !p.Collect) e.Add("Enable at least kill or collect.");
        if (p.ConfigSlots.Roaming is < 1 or > 2 || p.ConfigSlots.Flying is < 1 or > 2 || p.ConfigSlots.Shooting is < 1 or > 2) e.Add("Slots must be 1 or 2.");
        if (p.Safety.EmergencyHpPercent is < 1 or > 100) e.Add("Emergency HP: 1–100.");
        if (p.Safety.RepairHpPercent is < 1 or > 100) e.Add("Repair HP: 1–100.");
        if (p.Safety.FullHpPercent < p.Safety.RepairHpPercent || p.Safety.FullHpPercent > 100) e.Add("Full HP must be ≥ repair HP.");
        if (p.AdminDisconnect.CooldownMinutes < 0) e.Add("Admin cooldown ≥ 0.");
        if (p.DeathDisconnect.Enabled && p.DeathDisconnect.DeathThreshold <= 0) e.Add("Death threshold > 0.");
        if (p.DeathDisconnect.CooldownMinutes < 0) e.Add("Death cooldown ≥ 0.");
        ValidateResourceModule(p.Resources.Speed, "Speed", ProfileCatalog.ShieldSpeedEnrichmentOptions, e);
        ValidateResourceModule(p.Resources.Shields, "Shields", ProfileCatalog.ShieldSpeedEnrichmentOptions, e);
        ValidateResourceModule(p.Resources.Lasers, "Lasers", ProfileCatalog.LaserRocketEnrichmentOptions, e);
        ValidateResourceModule(p.Resources.Rockets, "Rockets", ProfileCatalog.LaserRocketEnrichmentOptions, e);
        foreach (var npc in p.NpcRules)
        {
            var variants = new[] { npc.DefaultVariant, npc.HyperVariant, npc.UltraVariant };
            foreach (var variant in variants)
            {
                if (variant.Range is < NpcVariantRuleViewModel.MinRange or > NpcVariantRuleViewModel.MaxRange)
                    e.Add($"NPC range must be {NpcVariantRuleViewModel.MinRange}-{NpcVariantRuleViewModel.MaxRange}.");
                if (variant.FollowOnLowHpPercent is < NpcVariantRuleViewModel.MinLowHpPercent or > NpcVariantRuleViewModel.MaxLowHpPercent)
                    e.Add($"Low HP threshold must be {NpcVariantRuleViewModel.MinLowHpPercent}-{NpcVariantRuleViewModel.MaxLowHpPercent}%.");
            }
        }
        return e;
    }

    private static void ValidateResourceModule(
        ResourceModuleSettings settings,
        string label,
        IReadOnlyList<EnrichmentMaterialOption> allowedOptions,
        List<string> errors)
    {
        if (settings.Priority is < 1 or > 4)
            errors.Add($"{label} priority must be 1-4.");

        if (!allowedOptions.Any(option => option.Value == settings.Material))
            errors.Add($"{label} material is invalid.");
    }

    private static ResourceAutomationSettings NormalizeResourceAutomation(ResourceAutomationSettings? resources)
    {
        resources ??= new ResourceAutomationSettings();
        resources.RefineIntervalSeconds = Math.Clamp(resources.RefineIntervalSeconds, 30, 600);
        resources.Speed ??= new ResourceModuleSettings { Material = EnrichmentMaterial.Uranit, Priority = 1 };
        resources.Shields ??= new ResourceModuleSettings { Material = EnrichmentMaterial.Uranit, Priority = 2 };
        resources.Lasers ??= new ResourceModuleSettings { Material = EnrichmentMaterial.Darkonit, Priority = 3 };
        resources.Rockets ??= new ResourceModuleSettings { Material = EnrichmentMaterial.Darkonit, Priority = 4 };

        NormalizeResourceModule(resources.Speed, ProfileCatalog.ShieldSpeedEnrichmentOptions, EnrichmentMaterial.Uranit);
        NormalizeResourceModule(resources.Shields, ProfileCatalog.ShieldSpeedEnrichmentOptions, EnrichmentMaterial.Uranit);
        NormalizeResourceModule(resources.Lasers, ProfileCatalog.LaserRocketEnrichmentOptions, EnrichmentMaterial.Darkonit);
        NormalizeResourceModule(resources.Rockets, ProfileCatalog.LaserRocketEnrichmentOptions, EnrichmentMaterial.Darkonit);

        var ordered = new[]
        {
            new { Settings = resources.Speed, Fallback = 1 },
            new { Settings = resources.Shields, Fallback = 2 },
            new { Settings = resources.Lasers, Fallback = 3 },
            new { Settings = resources.Rockets, Fallback = 4 },
        }
        .OrderBy(entry => entry.Settings.Priority)
        .ThenBy(entry => entry.Fallback)
        .ToArray();

        for (var index = 0; index < ordered.Length; index++)
        {
            ordered[index].Settings.Priority = index + 1;
        }

        return resources;
    }

    private static void NormalizeResourceModule(
        ResourceModuleSettings settings,
        IReadOnlyList<EnrichmentMaterialOption> allowedOptions,
        EnrichmentMaterial fallbackMaterial)
    {
        settings.Priority = Math.Clamp(settings.Priority, 1, 4);
        if (!allowedOptions.Any(option => option.Value == settings.Material))
        {
            settings.Material = fallbackMaterial;
        }
    }

    private void SyncResourceModuleAvailability()
    {
        foreach (var module in ResourceModules)
        {
            module.SetParentEnabled(ResourceAutomationEnabled);
        }
    }

    private static int ParseInt(string? text, int fallback) =>
        int.TryParse(text?.Trim(), out var v) ? v : fallback;

    private static string SanitizeId(string? name)
    {
        if (string.IsNullOrWhiteSpace(name)) return string.Empty;
        var chars = name.Trim().ToLowerInvariant()
            .Select(c => char.IsLetterOrDigit(c) ? c : c is ' ' or '.' ? '-' : c is '-' or '_' ? c : '\0')
            .Where(c => c != '\0');
        return new string(chars.ToArray());
    }

    /// <summary>
    /// Sets a string-backed int field with real-time clamping.
    /// While typing, allows empty/partial input. Once a valid int is parsed,
    /// clamps it to [min, max] and updates the text if it changed.
    /// </summary>
    private void ClampedIntSet(ref string field, string? value, int min, int max, string propertyName)
    {
        var text = value?.Trim() ?? string.Empty;

        // Allow empty while user is clearing the field
        if (string.IsNullOrEmpty(text))
        {
            if (field != text)
            {
                field = text;
                OnPropertyChanged(propertyName);
            }
            return;
        }

        if (int.TryParse(text, out var parsed))
        {
            var clamped = Math.Clamp(parsed, min, max);
            var clampedText = clamped.ToString();
            if (field != clampedText)
            {
                field = clampedText;
                OnPropertyChanged(propertyName);
            }
        }
        // else: ignore non-numeric input (don't update field)
    }

    private void HandleAvoidMapChanged(object? sender, PropertyChangedEventArgs e) { }

    private void SetCollectDuringCombatSilent(bool value)
    {
        if (_collectDuringCombat == value) return;
        _collectDuringCombat = value;
        OnPropertyChanged(nameof(CollectDuringCombat));
    }
}
