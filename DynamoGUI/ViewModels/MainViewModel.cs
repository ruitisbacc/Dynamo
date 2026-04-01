using Avalonia.Media;
using Avalonia.Threading;
using DynamoGUI.Models;
using DynamoGUI.Services;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;

namespace DynamoGUI.ViewModels;

public enum ShellSection
{
    Dashboard,
    Stats,
    Profiles,
}

public sealed class MainViewModel : ViewModelBase, IDisposable
{
    private static readonly IBrush RuntimeActiveBrush = new SolidColorBrush(Color.Parse("#22C55E"));
    private static readonly IBrush RuntimePausedBrush = new SolidColorBrush(Color.Parse("#EAB308"));
    private static readonly IBrush RuntimeIdleBrush = new SolidColorBrush(Color.Parse("#94A3B8"));

    private readonly AppSession _session;
    private readonly BackendService _backend;
    private readonly IpcClient _ipc;
    private readonly Dictionary<string, StatsEntryViewModel> _statsRows = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ResourceAmountViewModel> _resourceRows = new(StringComparer.Ordinal);
    private bool _disposed;

    public ProfileEditorViewModel ProfileEditor { get; }

    // ── Connection state ──

    private bool _backendRunning;
    public bool BackendRunning
    {
        get => _backendRunning;
        set
        {
            if (SetProperty(ref _backendRunning, value))
                OnPropertyChanged(nameof(BackendBadge));
        }
    }

    private bool _isConnected;
    public bool IsConnected
    {
        get => _isConnected;
        set
        {
            if (SetProperty(ref _isConnected, value))
            {
                OnPropertyChanged(nameof(CanStartBot));
                OnPropertyChanged(nameof(CanPauseBot));
                OnPropertyChanged(nameof(CanStopBot));
                OnPropertyChanged(nameof(ConnectionDotColor));
            }
        }
    }

    private bool _botRunning;
    public bool BotRunning
    {
        get => _botRunning;
        set
        {
            if (SetProperty(ref _botRunning, value))
            {
                OnPropertyChanged(nameof(BotBadge));
                OnPropertyChanged(nameof(BotPillColor));
                OnPropertyChanged(nameof(BotRuntimeBrush));
                OnPropertyChanged(nameof(CanStartBot));
                OnPropertyChanged(nameof(CanPauseBot));
                OnPropertyChanged(nameof(CanStopBot));
            }
        }
    }

    private bool _botPaused;
    public bool BotPaused
    {
        get => _botPaused;
        set
        {
            if (SetProperty(ref _botPaused, value))
            {
                OnPropertyChanged(nameof(BotBadge));
                OnPropertyChanged(nameof(BotPillColor));
                OnPropertyChanged(nameof(BotRuntimeBrush));
                OnPropertyChanged(nameof(CanStartBot));
                OnPropertyChanged(nameof(CanPauseBot));
                OnPropertyChanged(nameof(CanStopBot));
            }
        }
    }

    private string _connectionState = "Disconnected";
    public string ConnectionState
    {
        get => _connectionState;
        set
        {
            if (SetProperty(ref _connectionState, value))
                OnPropertyChanged(nameof(ConnectionDotColor));
        }
    }

    private string _engineState = "Offline";
    public string EngineState
    {
        get => _engineState;
        set => SetProperty(ref _engineState, value);
    }

    // ── Game data ──

    private string _activeProfile = "default";
    public string ActiveProfile { get => _activeProfile; set => SetProperty(ref _activeProfile, value); }

    private string _workingMap = "-";
    public string WorkingMap { get => _workingMap; set => SetProperty(ref _workingMap, value); }

    private string _currentMap = "-";
    public string CurrentMap { get => _currentMap; set => SetProperty(ref _currentMap, value); }

    private string _currentMode = "Idle";
    public string CurrentMode { get => _currentMode; set => SetProperty(ref _currentMode, value); }

    private string _currentTask = "Awaiting backend";
    public string CurrentTask { get => _currentTask; set => SetProperty(ref _currentTask, value); }

    private string _currentTarget = "-";
    public string CurrentTarget { get => _currentTarget; set => SetProperty(ref _currentTarget, value); }

    private string _targetCategory = "-";
    public string TargetCategory { get => _targetCategory; set => SetProperty(ref _targetCategory, value); }

    private bool _safetyActive;
    public bool SafetyActive { get => _safetyActive; set => SetProperty(ref _safetyActive, value); }

    private string _safetyReason = "-";
    public string SafetyReason { get => _safetyReason; set => SetProperty(ref _safetyReason, value); }

    private int _threatCount;
    public int ThreatCount { get => _threatCount; set => SetProperty(ref _threatCount, value); }

    private long _btc;
    public long Btc { get => _btc; set => SetProperty(ref _btc, value); }

    private long _plt;
    public long Plt { get => _plt; set => SetProperty(ref _plt, value); }

    private int _hpPercent;
    public int HpPercent { get => _hpPercent; set => SetProperty(ref _hpPercent, value); }

    private int _shieldPercent;
    public int ShieldPercent { get => _shieldPercent; set => SetProperty(ref _shieldPercent, value); }

    private int _cargoPercent;
    public int CargoPercent { get => _cargoPercent; set => SetProperty(ref _cargoPercent, value); }

    private int _heroX;
    public int HeroX { get => _heroX; set => SetProperty(ref _heroX, value); }

    private int _heroY;
    public int HeroY { get => _heroY; set => SetProperty(ref _heroY, value); }

    private int _heroTargetX;
    public int HeroTargetX { get => _heroTargetX; set => SetProperty(ref _heroTargetX, value); }

    private int _heroTargetY;
    public int HeroTargetY { get => _heroTargetY; set => SetProperty(ref _heroTargetY, value); }

    private bool _heroMoving;
    public bool HeroMoving { get => _heroMoving; set => SetProperty(ref _heroMoving, value); }

    private int _mapWidth = 21000;
    public int MapWidth { get => _mapWidth; set => SetProperty(ref _mapWidth, value); }

    private int _mapHeight = 13500;
    public int MapHeight { get => _mapHeight; set => SetProperty(ref _mapHeight, value); }

    private int _activeConfig = 1;
    public int ActiveConfig
    {
        get => _activeConfig;
        set
        {
            if (SetProperty(ref _activeConfig, value))
            {
                OnPropertyChanged(nameof(ConfigLabel));
            }
        }
    }

    private string _currentLaser = "-";
    public string CurrentLaser { get => _currentLaser; set => SetProperty(ref _currentLaser, value); }

    private string _currentRocket = "-";
    public string CurrentRocket { get => _currentRocket; set => SetProperty(ref _currentRocket, value); }

    private int _npcCount;
    public int NpcCount { get => _npcCount; set => SetProperty(ref _npcCount, value); }

    private int _enemyCount;
    public int EnemyCount { get => _enemyCount; set => SetProperty(ref _enemyCount, value); }

    private int _boxCount;
    public int BoxCount { get => _boxCount; set => SetProperty(ref _boxCount, value); }

    private int _portalCount;
    public int PortalCount { get => _portalCount; set => SetProperty(ref _portalCount, value); }

    private bool _hasTarget;
    public bool HasTarget { get => _hasTarget; set => SetProperty(ref _hasTarget, value); }

    private int _targetX;
    public int TargetX { get => _targetX; set => SetProperty(ref _targetX, value); }

    private int _targetY;
    public int TargetY { get => _targetY; set => SetProperty(ref _targetY, value); }

    private int _targetHpPercent;
    public int TargetHpPercent { get => _targetHpPercent; set => SetProperty(ref _targetHpPercent, value); }

    private int _targetShieldPercent;
    public int TargetShieldPercent { get => _targetShieldPercent; set => SetProperty(ref _targetShieldPercent, value); }

    private int _targetDistance;
    public int TargetDistance { get => _targetDistance; set => SetProperty(ref _targetDistance, value); }

    private string _heroName = string.Empty;
    public string HeroName { get => _heroName; set => SetProperty(ref _heroName, value); }

    private long _honor;
    public long Honor { get => _honor; set => SetProperty(ref _honor, value); }

    private long _experience;
    public long Experience { get => _experience; set => SetProperty(ref _experience, value); }

    private int _deathCount;
    public int DeathCount { get => _deathCount; set => SetProperty(ref _deathCount, value); }

    private long _botRuntimeSeconds;
    public long BotRuntimeSeconds
    {
        get => _botRuntimeSeconds;
        set
        {
            if (SetProperty(ref _botRuntimeSeconds, value))
            {
                OnPropertyChanged(nameof(BotRuntimeDisplay));
                OnPropertyChanged(nameof(BotRuntimeBrush));
            }
        }
    }

    private MapEntity[] _mapEntities = [];
    public MapEntity[] MapEntities { get => _mapEntities; set => SetProperty(ref _mapEntities, value); }

    // ── Module telemetry ──

    private string _combatState = "Idle";
    public string CombatState { get => _combatState; set => SetProperty(ref _combatState, value); }

    private string _combatDecision = "-";
    public string CombatDecision { get => _combatDecision; set => SetProperty(ref _combatDecision, value); }

    private string _combatMovement = "-";
    public string CombatMovement { get => _combatMovement; set => SetProperty(ref _combatMovement, value); }

    private string _travelState = "Idle";
    public string TravelState { get => _travelState; set => SetProperty(ref _travelState, value); }

    private string _travelDecision = "-";
    public string TravelDecision { get => _travelDecision; set => SetProperty(ref _travelDecision, value); }

    private string _travelDestination = "-";
    public string TravelDestination { get => _travelDestination; set => SetProperty(ref _travelDestination, value); }

    private string _roamingDecision = "Idle";
    public string RoamingDecision { get => _roamingDecision; set => SetProperty(ref _roamingDecision, value); }

    // ── Profiles ──

    private string _selectedProfile = "default";
    public string SelectedProfile { get => _selectedProfile; set => SetProperty(ref _selectedProfile, value); }

    public ObservableCollection<string> Profiles { get; } = new();
    public ObservableCollection<StatsSectionViewModel> StatsSections { get; } = new();
    public ObservableCollection<ResourceAmountViewModel> CurrentResourceRows { get; } = new();

    // ── Computed ──

    public bool CanStartBot => IsConnected && (!BotRunning || BotPaused);
    public bool CanPauseBot => IsConnected && BotRunning && !BotPaused;
    public bool CanStopBot => IsConnected && (BotRunning || BotPaused);
    public string BotBadge => BotPaused ? "Paused" : BotRunning ? "Running" : "Stopped";
    public Color BotPillColor => BotPaused
        ? Color.Parse("#EAB308")
        : BotRunning ? Color.Parse("#22C55E") : Color.Parse("#64748B");
    public Color ConnectionDotColor => IsConnected ? Color.Parse("#22C55E") : Color.Parse("#EF4444");
    public string BackendBadge => BackendRunning ? "Host Running" : "Host Offline";
    public string ConfigLabel => $"CFG {ActiveConfig}";
    public string HeroMovementLabel => HeroMoving ? "Moving" : "Holding";
    public string ThreatLabel => ThreatCount <= 0 ? "Clear" : $"{ThreatCount} threat(s)";
    public string TargetStatusLabel => HasTarget ? $"{TargetCategory} · {TargetDistance} u" : "No target";
    public string BotRuntimeDisplay => FormatDuration(BotRuntimeSeconds);
    public IBrush BotRuntimeBrush => BotPaused
        ? RuntimePausedBrush
        : BotRunning ? RuntimeActiveBrush : RuntimeIdleBrush;

    // ── Navigation ──

    private ShellSection _activeSection = ShellSection.Dashboard;
    public ShellSection ActiveSection
    {
        get => _activeSection;
        set
        {
            if (SetProperty(ref _activeSection, value))
            {
                OnPropertyChanged(nameof(IsDashboardSection));
                OnPropertyChanged(nameof(IsStatsSection));
                OnPropertyChanged(nameof(IsProfilesSection));
            }
        }
    }

    public bool IsDashboardSection => ActiveSection == ShellSection.Dashboard;
    public bool IsStatsSection => ActiveSection == ShellSection.Stats;
    public bool IsProfilesSection => ActiveSection == ShellSection.Profiles;

    // ── Constructor ──

    public MainViewModel()
        : this(new AppSession())
    {
    }

    public MainViewModel(AppSession session)
    {
        _session = session;
        _backend = session.Backend;
        _ipc = session.Ipc;
        ProfileEditor = new ProfileEditorViewModel(_ipc);
        InitializeStatsSections();
        InitializeResourceRows();

        Profiles.Add("default");
        SelectedProfile = "default";

        _backend.OnOutput += HandleBackendOutput;
        _backend.OnError += HandleBackendError;
        _backend.OnExited += HandleBackendExit;
        _ipc.OnConnected += HandleIpcConnected;
        _ipc.OnDisconnected += HandleIpcDisconnected;
        _ipc.OnError += HandleIpcError;
        _ipc.OnStatusSnapshot += HandleStatusSnapshot;
        _ipc.OnProfilesSnapshot += HandleProfilesSnapshot;

        BackendRunning = _backend.IsRunning;
        IsConnected = _ipc.IsConnected;
    }

    // ── Navigation ──

    public void ShowDashboard() => ActiveSection = ShellSection.Dashboard;
    public void ShowStats() => ActiveSection = ShellSection.Stats;
    public void ShowProfiles() => ActiveSection = ShellSection.Profiles;

    // ── Commands ──

    public async Task RefreshAsync()
    {
        if (!IsConnected)
            return;
        await _ipc.RequestStatusAsync();
        await _ipc.RequestProfilesAsync();
    }

    public async Task StartBotAsync()
    {
        if (!CanStartBot) return;
        await _ipc.StartBotAsync();
    }

    public async Task StopBotAsync()
    {
        if (!CanStopBot) return;
        await _ipc.StopBotAsync();
    }

    public async Task PauseBotAsync()
    {
        if (!CanPauseBot) return;
        await _ipc.PauseBotAsync();
    }

    public async Task MoveToAsync(int gameX, int gameY)
    {
        if (!IsConnected) return;
        await _ipc.MoveToAsync(gameX, gameY);
    }

    public async Task DisconnectAsync()
    {
        try
        {
            if (IsConnected)
            {
                await _ipc.RequestShutdownAsync();
                await Task.Delay(300);
            }
        }
        catch { }

        _ipc.Disconnect();
        if (_backend.IsRunning) _backend.Stop();
        BackendRunning = false;
        IsConnected = false;
        BotRunning = false;
        BotPaused = false;
    }

    // ── Event handlers ──

    private void HandleBackendOutput(string message)
    {
        Dispatcher.UIThread.Post(() =>
        {
            BackendRunning = _backend.IsRunning;
        });
    }

    private void HandleBackendError(string message)
    {
        Dispatcher.UIThread.Post(() =>
        {
            BackendRunning = _backend.IsRunning;
        });
    }

    private void HandleBackendExit(int exitCode)
    {
        Dispatcher.UIThread.Post(() =>
        {
            BackendRunning = false;
            IsConnected = false;
            BotRunning = false;
            BotPaused = false;
        });
    }

    private void HandleIpcConnected()
    {
        Dispatcher.UIThread.Post(() =>
        {
            IsConnected = true;
        });
    }

    private void HandleIpcDisconnected()
    {
        Dispatcher.UIThread.Post(() =>
        {
            IsConnected = false;
            BotRunning = false;
            BotPaused = false;
        });
    }

    private void HandleIpcError(string message) { }

    private void HandleStatusSnapshot(BackendStatusSnapshot snapshot)
    {
        Dispatcher.UIThread.Post(() =>
        {
            // ── Batch map-critical fields: set backing fields directly, fire once ──
            // This prevents cascading PropertyChanged → binding → render invalidation
            // storms when the map control reads these during its composition frame.
            _heroX = snapshot.HeroX;
            _heroY = snapshot.HeroY;
            _heroTargetX = snapshot.HeroTargetX;
            _heroTargetY = snapshot.HeroTargetY;
            _heroMoving = snapshot.HeroMoving;
            _targetX = snapshot.TargetX;
            _targetY = snapshot.TargetY;
            _hasTarget = snapshot.HasTarget;
            if (snapshot.MapWidth > 0) _mapWidth = snapshot.MapWidth;
            if (snapshot.MapHeight > 0) _mapHeight = snapshot.MapHeight;
            _activeConfig = snapshot.ActiveConfig;
            _mapEntities = snapshot.MapEntities;

            // Fire PropertyChanged for the batched map fields
            OnPropertyChanged(nameof(HeroX));
            OnPropertyChanged(nameof(HeroY));
            OnPropertyChanged(nameof(HeroTargetX));
            OnPropertyChanged(nameof(HeroTargetY));
            OnPropertyChanged(nameof(HeroMoving));
            OnPropertyChanged(nameof(TargetX));
            OnPropertyChanged(nameof(TargetY));
            OnPropertyChanged(nameof(HasTarget));
            OnPropertyChanged(nameof(MapWidth));
            OnPropertyChanged(nameof(MapHeight));
            OnPropertyChanged(nameof(ActiveConfig));
            OnPropertyChanged(nameof(MapEntities));

            // ── Non-critical UI fields: normal SetProperty is fine ──
            ConnectionState = snapshot.ConnectionState;
            EngineState = snapshot.EngineState;
            ActiveProfile = snapshot.ActiveProfile;
            WorkingMap = snapshot.WorkingMap;
            CurrentMap = snapshot.CurrentMap;
            CurrentMode = snapshot.CurrentMode;
            CurrentTask = snapshot.CurrentTask;
            CurrentTarget = snapshot.CurrentTarget;
            TargetCategory = snapshot.TargetCategory;
            SafetyActive = snapshot.SafetyActive;
            SafetyReason = snapshot.SafetyReason;
            ThreatCount = snapshot.ThreatCount;
            Btc = snapshot.Btc;
            Plt = snapshot.Plt;
            HpPercent = snapshot.HpPercent;
            ShieldPercent = snapshot.ShieldPercent;
            CargoPercent = snapshot.CargoPercent;
            CurrentLaser = snapshot.CurrentLaser;
            CurrentRocket = snapshot.CurrentRocket;
            NpcCount = snapshot.NpcCount;
            EnemyCount = snapshot.EnemyCount;
            BoxCount = snapshot.BoxCount;
            PortalCount = snapshot.PortalCount;
            TargetHpPercent = snapshot.TargetHpPercent;
            TargetShieldPercent = snapshot.TargetShieldPercent;
            TargetDistance = snapshot.TargetDistance;
            HeroName = snapshot.HeroName;
            Honor = snapshot.Honor;
            Experience = snapshot.Experience;
            DeathCount = snapshot.DeathCount;
            BotRuntimeSeconds = Math.Max(0, snapshot.Stats.RuntimeMs / 1000);
            CombatState = snapshot.CombatState;
            CombatDecision = snapshot.CombatDecision;
            CombatMovement = snapshot.CombatMovement;
            TravelState = snapshot.TravelState;
            TravelDecision = snapshot.TravelDecision;
            TravelDestination = snapshot.TravelDestination;
            RoamingDecision = snapshot.RoamingDecision;
            BotRunning = snapshot.BotRunning;
            BotPaused = snapshot.BotPaused;
            ApplyStatsSnapshot(snapshot.Stats);
            ApplyCurrentResources(snapshot.CurrentResources);

            OnPropertyChanged(nameof(BackendBadge));
            OnPropertyChanged(nameof(HeroMovementLabel));
            OnPropertyChanged(nameof(ThreatLabel));
            OnPropertyChanged(nameof(TargetStatusLabel));
            OnPropertyChanged(nameof(ConfigLabel));

            if (!string.IsNullOrWhiteSpace(snapshot.ActiveProfile))
                SelectedProfile = snapshot.ActiveProfile;
        }, Avalonia.Threading.DispatcherPriority.Render);
    }

    private void HandleProfilesSnapshot(ProfileListSnapshot snapshot)
    {
        Dispatcher.UIThread.Post(() =>
        {
            Profiles.Clear();
            foreach (var profile in snapshot.Profiles.Where(p => !string.IsNullOrWhiteSpace(p)).Distinct(StringComparer.Ordinal))
                Profiles.Add(profile);

            if (!Profiles.Contains(snapshot.ActiveProfile) && !string.IsNullOrWhiteSpace(snapshot.ActiveProfile))
                Profiles.Add(snapshot.ActiveProfile);

            ActiveProfile = string.IsNullOrWhiteSpace(snapshot.ActiveProfile) ? ActiveProfile : snapshot.ActiveProfile;
            SelectedProfile = Profiles.Contains(ActiveProfile) ? ActiveProfile : Profiles.FirstOrDefault() ?? "default";
        });
    }

    private void InitializeStatsSections()
    {
        StatsSections.Clear();
        _statsRows.Clear();

        StatsSections.Add(CreateStatsSection(
            "Resources",
            ("plt", "PLT"),
            ("btc", "BTC"),
            ("experience", "EXP"),
            ("honor", "HON")));

        StatsSections.Add(CreateStatsSection(
            "Lasers",
            ("laser_rlx1", "RLX-1"),
            ("laser_glx2", "GLX-2"),
            ("laser_blx3", "BLX-3"),
            ("laser_wlx4", "WLX-4"),
            ("laser_glx2as", "GLX-2-AS"),
            ("laser_mrs6x", "MRS-6X")));

        StatsSections.Add(CreateStatsSection(
            "Rockets",
            ("rocket_kep410", "KEP-410"),
            ("rocket_nc30", "NC-30"),
            ("rocket_tnc130", "TNC-130")));

        StatsSections.Add(CreateStatsSection(
            "Energy",
            ("energy_ee", "EE"),
            ("energy_en", "EN"),
            ("energy_eg", "EG"),
            ("energy_em", "EM")));
    }

    private void InitializeResourceRows()
    {
        CurrentResourceRows.Clear();
        _resourceRows.Clear();

        AddResourceRow("cerium", "Cerium");
        AddResourceRow("mercury", "Mercury");
        AddResourceRow("erbium", "Erbium");
        AddResourceRow("piritid", "Piritid");
        AddResourceRow("darkonit", "Darkonit");
        AddResourceRow("uranit", "Uranit");
        AddResourceRow("azurit", "Azurit");
        AddResourceRow("dungid", "Dungid");
        AddResourceRow("xureon", "Xureon");
    }

    private void AddResourceRow(string key, string label)
    {
        var row = new ResourceAmountViewModel(label);
        CurrentResourceRows.Add(row);
        _resourceRows[key] = row;
    }

    private StatsSectionViewModel CreateStatsSection(
        string title,
        params (string Key, string Label)[] rows)
    {
        var section = new StatsSectionViewModel(title);
        foreach (var (key, label) in rows)
        {
            var row = new StatsEntryViewModel(label);
            section.Rows.Add(row);
            _statsRows[key] = row;
        }

        return section;
    }

    private void ApplyStatsSnapshot(SessionStatsSnapshot? stats)
    {
        stats ??= new SessionStatsSnapshot();
        var session = stats.Session ?? new InventoryStatsSnapshot();
        var total = stats.Total ?? new InventoryStatsSnapshot();

        UpdateStatsRow("plt", session.Plt, total.Plt);
        UpdateStatsRow("btc", session.Btc, total.Btc);
        UpdateStatsRow("experience", session.Experience, total.Experience);
        UpdateStatsRow("honor", session.Honor, total.Honor);

        UpdateStatsRow("laser_rlx1", session.LaserRlx1, total.LaserRlx1);
        UpdateStatsRow("laser_glx2", session.LaserGlx2, total.LaserGlx2);
        UpdateStatsRow("laser_blx3", session.LaserBlx3, total.LaserBlx3);
        UpdateStatsRow("laser_wlx4", session.LaserWlx4, total.LaserWlx4);
        UpdateStatsRow("laser_glx2as", session.LaserGlx2As, total.LaserGlx2As);
        UpdateStatsRow("laser_mrs6x", session.LaserMrs6X, total.LaserMrs6X);

        UpdateStatsRow("rocket_kep410", session.RocketKep410, total.RocketKep410);
        UpdateStatsRow("rocket_nc30", session.RocketNc30, total.RocketNc30);
        UpdateStatsRow("rocket_tnc130", session.RocketTnc130, total.RocketTnc130);

        UpdateStatsRow("energy_ee", session.EnergyEe, total.EnergyEe);
        UpdateStatsRow("energy_en", session.EnergyEn, total.EnergyEn);
        UpdateStatsRow("energy_eg", session.EnergyEg, total.EnergyEg);
        UpdateStatsRow("energy_em", session.EnergyEm, total.EnergyEm);
    }

    private void UpdateStatsRow(string key, long session, long total)
    {
        if (_statsRows.TryGetValue(key, out var row))
            row.Update(session, total);
    }

    private void ApplyCurrentResources(ResourceInventorySnapshot? resources)
    {
        resources ??= new ResourceInventorySnapshot();

        UpdateResourceRow("cerium", resources.Cerium);
        UpdateResourceRow("mercury", resources.Mercury);
        UpdateResourceRow("erbium", resources.Erbium);
        UpdateResourceRow("piritid", resources.Piritid);
        UpdateResourceRow("darkonit", resources.Darkonit);
        UpdateResourceRow("uranit", resources.Uranit);
        UpdateResourceRow("azurit", resources.Azurit);
        UpdateResourceRow("dungid", resources.Dungid);
        UpdateResourceRow("xureon", resources.Xureon);
    }

    private void UpdateResourceRow(string key, long amount)
    {
        if (_resourceRows.TryGetValue(key, out var row))
            row.Update(amount);
    }

    private static string FormatDuration(long totalSeconds)
    {
        var safeSeconds = Math.Max(0, totalSeconds);
        var span = TimeSpan.FromSeconds(safeSeconds);
        return span.TotalHours >= 100
            ? $"{(int)span.TotalHours:000}:{span.Minutes:00}:{span.Seconds:00}"
            : $"{(int)span.TotalHours:00}:{span.Minutes:00}:{span.Seconds:00}";
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        ProfileEditor.Dispose();
        _session.Dispose();
    }
}
