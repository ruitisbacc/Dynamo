using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using DynamoGUI.Models;

namespace DynamoGUI.Controls;

public sealed class TacticalMapView : Control
{
    private const double MapPadding = 10;
    private const double MapBackgroundOpacity = 0.22;
    private const int TrailCapacity = 320;
    private const double TrailMinDistance = 25;
    private const double TrailMaxGap = 500;
    private const long TrailMaxAgeMs = 10_000;

    // ── Styled properties (NO AffectsRender — the composition loop calls InvalidateVisual every frame) ──

    public static readonly StyledProperty<int> HeroXProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(HeroX));
    public static readonly StyledProperty<int> HeroYProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(HeroY));
    public static readonly StyledProperty<int> HeroTargetXProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(HeroTargetX));
    public static readonly StyledProperty<int> HeroTargetYProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(HeroTargetY));
    public static readonly StyledProperty<bool> HeroMovingProperty =
        AvaloniaProperty.Register<TacticalMapView, bool>(nameof(HeroMoving));
    public static readonly StyledProperty<int> MapWidthProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(MapWidth), 21000);
    public static readonly StyledProperty<int> MapHeightProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(MapHeight), 13500);
    public static readonly StyledProperty<string?> CurrentMapProperty =
        AvaloniaProperty.Register<TacticalMapView, string?>(nameof(CurrentMap));
    public static readonly StyledProperty<bool> HasTargetProperty =
        AvaloniaProperty.Register<TacticalMapView, bool>(nameof(HasTarget));
    public static readonly StyledProperty<int> TargetXProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(TargetX));
    public static readonly StyledProperty<int> TargetYProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(TargetY));
    public static readonly StyledProperty<int> ActiveConfigProperty =
        AvaloniaProperty.Register<TacticalMapView, int>(nameof(ActiveConfig), 1);
    public static readonly StyledProperty<MapEntity[]> MapEntitiesProperty =
        AvaloniaProperty.Register<TacticalMapView, MapEntity[]>(nameof(MapEntities), []);

    // ── Pre-allocated brushes/pens ──

    private static readonly SolidColorBrush MapBgBrush = new(Color.Parse("#060A0F"));
    private static readonly SolidColorBrush MapVeilBrush = new(Color.FromArgb(52, 6, 10, 15));
    private static readonly SolidColorBrush HeroWhiteBrush = new(Color.Parse("#F0F6FC"));

    private static readonly SolidColorBrush NpcBrush = new(Color.Parse("#EF4444"));
    private static readonly SolidColorBrush EnemyBrush = new(Color.Parse("#EAB308"));
    private static readonly SolidColorBrush AllyBrush = new(Color.Parse("#22C55E"));
    private static readonly SolidColorBrush BoxBrush = new(Color.Parse("#A855F7"));
    private static readonly SolidColorBrush PortalBrush = new(Color.Parse("#3B82F6"));
    private static readonly SolidColorBrush StationBrush = new(Color.Parse("#C8A050"));

    private static readonly SolidColorBrush TargetFillBrush = new(Color.Parse("#EAB308"));
    private static readonly SolidColorBrush TargetGlowBrush = new(Color.FromArgb(50, 255, 255, 255));
    private static readonly Pen TargetRingPen = new(new SolidColorBrush(Color.FromArgb(180, 255, 255, 255)), 1);
    private static readonly Pen TargetLinePen = new(new SolidColorBrush(Color.Parse("#EAB308")), 1.5,
        new DashStyle([5, 4], 0));

    private static readonly Pen MoveGlowPen = new(new SolidColorBrush(Color.FromArgb(40, 65, 105, 225)), 4);
    private static readonly Pen MoveLinePen = new(new SolidColorBrush(Color.FromArgb(180, 65, 105, 225)), 1.5);

    private static readonly Pen PortalRingPen = new(PortalBrush, 2);
    private static readonly Pen StationBorderPen = new(new SolidColorBrush(Color.Parse("#696969")), 1);

    private static readonly Pen BorderGlowOuterPen = new(new SolidColorBrush(Color.FromArgb(30, 0, 140, 180)), 3);
    private static readonly Pen BorderMainPen = new(new SolidColorBrush(Color.FromArgb(100, 0, 140, 180)), 1);

    private static readonly SolidColorBrush NameNpcBrush = new(Color.FromArgb(160, 239, 68, 68));
    private static readonly SolidColorBrush NameEnemyBrush = new(Color.FromArgb(160, 234, 179, 8));
    private static readonly SolidColorBrush NameAllyBrush = new(Color.FromArgb(160, 34, 197, 94));
    private static readonly SolidColorBrush NamePortalBrush = new(Color.FromArgb(120, 59, 130, 246));
    private static readonly SolidColorBrush NameStationBrush = new(Color.FromArgb(100, 200, 160, 80));
    private static readonly SolidColorBrush NameShadowBrush = new(Color.FromArgb(160, 0, 0, 0));
    private static readonly Typeface NameTypeface = new("Segoe UI");

    private static readonly Pen[] TrailPens;

    static TacticalMapView()
    {
        // No AffectsRender — composition loop drives rendering.
        TrailPens = new Pen[16];
        for (var i = 0; i < 16; i++)
            TrailPens[i] = new Pen(new SolidColorBrush(Color.FromArgb((byte)(15 + i * 5), 255, 255, 255)), 1);
    }

    // ── High-resolution timing via Stopwatch ──
    private static long NowMs() => Stopwatch.GetTimestamp() / (Stopwatch.Frequency / 1000);

    // ── Render timer at Render priority for frame-aligned updates ──
    private readonly DispatcherTimer _renderTimer;

    // ── Entity interpolation (keyed by entity ID) ──
    private readonly Dictionary<int, EntityMotion> _entities = new(256);
    private readonly HashSet<int> _seenIds = new(256);
    private MapEntity[]? _lastEntities;

    private struct EntityMotion
    {
        public double StartX, StartY;
        public double TargetX, TargetY;
        public double DisplayX, DisplayY;
        public long StartTimeMs;
        public long DurationMs;
        public int Type;
        public string Name;
    }

    // ── Hero interpolation ──
    private double _dispHeroX, _dispHeroY;
    private double _dispTargetX, _dispTargetY;
    private double _dispDestX, _dispDestY;
    private double _heroStartX, _heroStartY;
    private double _heroGoalX, _heroGoalY;
    private long _heroMoveStartMs;
    private long _heroMoveDurationMs = 100;
    private long _lastSnapshotMs;
    private bool _interpInit;
    private string? _backgroundMapKey;
    private Bitmap? _backgroundBitmap;

    // ── Trail (circular buffer — O(1) add/remove) ──
    private readonly TrailPoint[] _trailBuffer = new TrailPoint[TrailCapacity];
    private int _trailHead; // index of oldest element
    private int _trailCount;
    private double _lastTrailX, _lastTrailY;
    private bool _trailInit;

    private struct TrailPoint
    {
        public double GameX, GameY;
        public long TimestampMs;
    }

    // ── Text cache ──
    private readonly Dictionary<int, (FormattedText text, FormattedText shadow)> _textCache = new(128);

    // ── Click-to-move / drag ──
    private bool _isDragging;
    private bool _hasLocalMovePreview;
    private double _localMovePreviewX, _localMovePreviewY;
    private long _localMovePreviewExpiryMs;
    private long _lastMoveCommandMs;
    private const long MoveThrottleMs = 33;
    private const long LocalMovePreviewHoldMs = 900;
    private const double LocalMovePreviewClearDistance = 8;

    public event Action<int, int>? MapClicked;

    public TacticalMapView()
    {
        ClipToBounds = true;
        _renderTimer = new DispatcherTimer(DispatcherPriority.Render)
        {
            Interval = TimeSpan.FromMilliseconds(16) // ~60fps target, but at Render priority
        };
        _renderTimer.Tick += OnRenderTick;
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        _renderTimer.Start();
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        _renderTimer.Stop();
        base.OnDetachedFromVisualTree(e);
    }

    private void OnRenderTick(object? sender, EventArgs e)
    {
        var now = NowMs();
        ProcessNewSnapshot(now);
        InterpolateAll(now);
        UpdateTrail(now);
        UpdateLocalMovePreview(now);
        InvalidateVisual();
    }

    public int HeroX { get => GetValue(HeroXProperty); set => SetValue(HeroXProperty, value); }
    public int HeroY { get => GetValue(HeroYProperty); set => SetValue(HeroYProperty, value); }
    public int HeroTargetX { get => GetValue(HeroTargetXProperty); set => SetValue(HeroTargetXProperty, value); }
    public int HeroTargetY { get => GetValue(HeroTargetYProperty); set => SetValue(HeroTargetYProperty, value); }
    public bool HeroMoving { get => GetValue(HeroMovingProperty); set => SetValue(HeroMovingProperty, value); }
    public int MapWidth { get => GetValue(MapWidthProperty); set => SetValue(MapWidthProperty, value); }
    public int MapHeight { get => GetValue(MapHeightProperty); set => SetValue(MapHeightProperty, value); }
    public string? CurrentMap { get => GetValue(CurrentMapProperty); set => SetValue(CurrentMapProperty, value); }
    public bool HasTarget { get => GetValue(HasTargetProperty); set => SetValue(HasTargetProperty, value); }
    public int TargetX { get => GetValue(TargetXProperty); set => SetValue(TargetXProperty, value); }
    public int TargetY { get => GetValue(TargetYProperty); set => SetValue(TargetYProperty, value); }
    public int ActiveConfig { get => GetValue(ActiveConfigProperty); set => SetValue(ActiveConfigProperty, value); }
    public MapEntity[] MapEntities { get => GetValue(MapEntitiesProperty); set => SetValue(MapEntitiesProperty, value); }

    private void ProcessNewSnapshot(long now)
    {
        var entities = MapEntities;
        if (entities == _lastEntities) return;
        _lastEntities = entities;

        var snapshotIntervalMs = _lastSnapshotMs > 0
            ? Math.Clamp(now - _lastSnapshotMs, 16L, 120L)
            : 100L;
        _lastSnapshotMs = now;
        _heroMoveDurationMs = snapshotIntervalMs;

        // Hero: set up linear interpolation from current display to new position
        if (!_interpInit)
        {
            _dispHeroX = HeroX; _dispHeroY = HeroY;
            _dispTargetX = TargetX; _dispTargetY = TargetY;
            _dispDestX = HeroTargetX; _dispDestY = HeroTargetY;
            _interpInit = true;
        }
        _heroStartX = _dispHeroX; _heroStartY = _dispHeroY;
        _heroGoalX = HeroX; _heroGoalY = HeroY;
        _heroMoveStartMs = now;

        // Entities
        _seenIds.Clear();

        if (entities is { Length: > 0 })
        {
            foreach (var e in entities)
            {
                _seenIds.Add(e.Id);

                if (_entities.TryGetValue(e.Id, out var motion))
                {
                    var dx = e.X - motion.DisplayX;
                    var dy = e.Y - motion.DisplayY;
                    var dist = Math.Sqrt(dx * dx + dy * dy);

                    motion.StartX = motion.DisplayX;
                    motion.StartY = motion.DisplayY;
                    motion.TargetX = e.X;
                    motion.TargetY = e.Y;
                    motion.StartTimeMs = now;
                    motion.DurationMs = Math.Clamp((long)(dist * 0.8), 50, 400);
                    motion.Type = e.Type;

                    if (motion.Name != e.Name)
                    {
                        _textCache.Remove(e.Id);
                        motion.Name = e.Name;
                    }

                    _entities[e.Id] = motion;
                }
                else
                {
                    _entities[e.Id] = new EntityMotion
                    {
                        StartX = e.X, StartY = e.Y,
                        TargetX = e.X, TargetY = e.Y,
                        DisplayX = e.X, DisplayY = e.Y,
                        StartTimeMs = now,
                        DurationMs = 1,
                        Type = e.Type,
                        Name = e.Name,
                    };
                }
            }
        }

        // Remove entities not in this snapshot
        List<int>? toRemove = null;
        foreach (var (id, _) in _entities)
        {
            if (!_seenIds.Contains(id))
            {
                toRemove ??= new(16);
                toRemove.Add(id);
            }
        }
        if (toRemove != null)
            foreach (var id in toRemove)
            {
                _entities.Remove(id);
                _textCache.Remove(id);
            }
    }

    private void InterpolateAll(long now)
    {
        // Hero
        if (_interpInit)
        {
            var heroT = HeroInterpT(now);
            _dispHeroX = _heroStartX + (_heroGoalX - _heroStartX) * heroT;
            _dispHeroY = _heroStartY + (_heroGoalY - _heroStartY) * heroT;

            // Target/dest: simple lerp
            const double lt = 0.2;
            _dispTargetX += (TargetX - _dispTargetX) * lt;
            _dispTargetY += (TargetY - _dispTargetY) * lt;
            _dispDestX += (HeroTargetX - _dispDestX) * lt;
            _dispDestY += (HeroTargetY - _dispDestY) * lt;
        }

        // Entities
        foreach (var id in _entities.Keys)
        {
            var m = _entities[id];
            var elapsed = now - m.StartTimeMs;
            var t = m.DurationMs > 0 ? Math.Min(1.0, (double)elapsed / m.DurationMs) : 1.0;

            // Ease-out: t' = 1 - (1-t)^2
            var eased = 1.0 - (1.0 - t) * (1.0 - t);

            m.DisplayX = m.StartX + (m.TargetX - m.StartX) * eased;
            m.DisplayY = m.StartY + (m.TargetY - m.StartY) * eased;
            _entities[id] = m;
        }
    }

    private double HeroInterpT(long now)
    {
        var elapsed = now - _heroMoveStartMs;
        var durationMs = Math.Max(_heroMoveDurationMs, 16);
        var t = Math.Min(1.0, (double)elapsed / durationMs);
        return 1.0 - (1.0 - t) * (1.0 - t); // ease-out
    }

    // ── Circular buffer trail helpers ──

    private void TrailAdd(double gx, double gy, long timeMs)
    {
        var idx = (_trailHead + _trailCount) % TrailCapacity;
        _trailBuffer[idx] = new TrailPoint { GameX = gx, GameY = gy, TimestampMs = timeMs };
        if (_trailCount < TrailCapacity)
            _trailCount++;
        else
            _trailHead = (_trailHead + 1) % TrailCapacity; // overwrite oldest
    }

    private ref TrailPoint TrailAt(int logicalIndex)
        => ref _trailBuffer[(_trailHead + logicalIndex) % TrailCapacity];

    private void TrailRemoveOldest()
    {
        if (_trailCount <= 0) return;
        _trailHead = (_trailHead + 1) % TrailCapacity;
        _trailCount--;
    }

    private void TrailClear()
    {
        _trailHead = 0;
        _trailCount = 0;
    }

    private void UpdateTrail(long now)
    {
        var hx = _dispHeroX;
        var hy = _dispHeroY;

        if (!_trailInit) { _lastTrailX = hx; _lastTrailY = hy; _trailInit = true; return; }

        var dx = hx - _lastTrailX;
        var dy = hy - _lastTrailY;
        var dist = Math.Sqrt(dx * dx + dy * dy);

        if (dist > TrailMaxGap) { TrailClear(); _lastTrailX = hx; _lastTrailY = hy; return; }

        if (dist >= TrailMinDistance)
        {
            TrailAdd(hx, hy, now);
            _lastTrailX = hx; _lastTrailY = hy;
        }

        // Expire old trail points (O(1) per removal)
        var expiry = now - TrailMaxAgeMs;
        while (_trailCount > 0 && TrailAt(0).TimestampMs < expiry)
            TrailRemoveOldest();
    }

    private void UpdateLocalMovePreview(long now)
    {
        if (!_hasLocalMovePreview) return;
        if (_isDragging) return;

        if (now >= _localMovePreviewExpiryMs)
        {
            _hasLocalMovePreview = false;
            return;
        }

        if (DistanceSquared(HeroTargetX, HeroTargetY, _localMovePreviewX, _localMovePreviewY) <=
            LocalMovePreviewClearDistance * LocalMovePreviewClearDistance)
        {
            _hasLocalMovePreview = false;
            return;
        }

        if (!HeroMoving &&
            DistanceSquared(HeroX, HeroY, _localMovePreviewX, _localMovePreviewY) <=
            LocalMovePreviewClearDistance * LocalMovePreviewClearDistance)
        {
            _hasLocalMovePreview = false;
        }
    }

    private void SetLocalMovePreview(int gameX, int gameY, long now)
    {
        _hasLocalMovePreview = true;
        _localMovePreviewX = gameX;
        _localMovePreviewY = gameY;
        _localMovePreviewExpiryMs = now + LocalMovePreviewHoldMs;
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed) return;

        _isDragging = true;
        e.Pointer.Capture(this);
        FireMoveFromPointer(point.Position, force: true);
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        if (!_isDragging) return;

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            _isDragging = false;
            e.Pointer.Capture(null);
            return;
        }

        FireMoveFromPointer(point.Position, force: false);
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        if (_isDragging)
        {
            _isDragging = false;
            FireMoveFromPointer(e.GetPosition(this), force: true);
        }
        e.Pointer.Capture(null);
    }

    private void FireMoveFromPointer(Point screenPos, bool force)
    {
        var bounds = Bounds;
        var renderW = bounds.Width - 2 * MapPadding;
        var renderH = bounds.Height - 2 * MapPadding;
        if (renderW <= 0 || renderH <= 0) return;

        var now = NowMs();
        var mw = Math.Max(MapWidth, 1);
        var mh = Math.Max(MapHeight, 1);
        var gameX = Math.Clamp((int)((screenPos.X - MapPadding) / renderW * mw), 0, mw);
        var gameY = Math.Clamp((int)((1.0 - (screenPos.Y - MapPadding) / renderH) * mh), 0, mh);

        SetLocalMovePreview(gameX, gameY, now);

        if (!force && now - _lastMoveCommandMs < MoveThrottleMs) return;
        _lastMoveCommandMs = now;
        MapClicked?.Invoke(gameX, gameY);
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);

        var bounds = Bounds;
        if (bounds.Width <= 0 || bounds.Height <= 0) return;

        context.DrawRectangle(MapBgBrush, null, bounds);

        var renderW = bounds.Width - 2 * MapPadding;
        var renderH = bounds.Height - 2 * MapPadding;
        if (renderW <= 0 || renderH <= 0) return;

        var mw = (double)Math.Max(MapWidth, 1);
        var mh = (double)Math.Max(MapHeight, 1);
        var sx = renderW / mw;
        var sy = renderH / mh;

        RenderMapBackground(context, renderW, renderH);

        // ── Trail ──
        if (_trailCount >= 2)
        {
            for (var i = 1; i < _trailCount; i++)
            {
                var bucket = i * (TrailPens.Length - 1) / _trailCount;
                ref var prev = ref TrailAt(i - 1);
                ref var cur = ref TrailAt(i);
                var from = ToScreen(prev.GameX, prev.GameY, sx, sy, renderH);
                var to = ToScreen(cur.GameX, cur.GameY, sx, sy, renderH);
                context.DrawLine(TrailPens[bucket], from, to);
            }
        }

        // ── Entities (depth-sorted passes) ──
        RenderEntitiesByType(context, 5, sx, sy, renderH); // stations
        RenderEntitiesByType(context, 4, sx, sy, renderH); // portals
        RenderEntitiesByType(context, 3, sx, sy, renderH); // boxes
        RenderEntitiesByType(context, 0, sx, sy, renderH); // npcs
        RenderEntitiesByType(context, 1, sx, sy, renderH); // enemies
        RenderEntitiesByType(context, 2, sx, sy, renderH); // allies

        // ── Movement line ──
        var heroScreen = ToScreen(_dispHeroX, _dispHeroY, sx, sy, renderH);
        var now = NowMs();

        var hasLocalPreview = _hasLocalMovePreview && now < _localMovePreviewExpiryMs;
        var hasBackendDestination = (HeroMoving || HeroTargetX > 0 || HeroTargetY > 0) &&
            (HeroTargetX != HeroX || HeroTargetY != HeroY);
        if (hasLocalPreview || hasBackendDestination)
        {
            var destScreen = hasLocalPreview
                ? ToScreen(_localMovePreviewX, _localMovePreviewY, sx, sy, renderH)
                : ToScreen(_dispDestX, _dispDestY, sx, sy, renderH);
            context.DrawLine(MoveGlowPen, heroScreen, destScreen);
            context.DrawLine(MoveLinePen, heroScreen, destScreen);
            context.DrawLine(MoveLinePen,
                new Point(destScreen.X - 4, destScreen.Y), new Point(destScreen.X + 4, destScreen.Y));
            context.DrawLine(MoveLinePen,
                new Point(destScreen.X, destScreen.Y - 4), new Point(destScreen.X, destScreen.Y + 4));
        }

        // ── Target ──
        if (HasTarget && TargetX > 0 && TargetY > 0)
        {
            var targetScreen = ToScreen(_dispTargetX, _dispTargetY, sx, sy, renderH);
            context.DrawLine(TargetLinePen, heroScreen, targetScreen);
            context.DrawEllipse(TargetGlowBrush, null, targetScreen, 10, 10);
            context.DrawEllipse(null, TargetRingPen, targetScreen, 8, 8);
            context.DrawEllipse(TargetFillBrush, null, targetScreen, 3, 3);
        }

        // ── Hero ──
        context.DrawEllipse(HeroWhiteBrush, null, heroScreen, 4, 4);

        // ── Border ──
        var borderRect = new Rect(MapPadding - 1, MapPadding - 1, renderW + 2, renderH + 2);
        context.DrawRectangle(null, BorderGlowOuterPen, borderRect);
        context.DrawRectangle(null, BorderMainPen, borderRect);
    }

    private void RenderMapBackground(DrawingContext context, double renderW, double renderH)
    {
        var mapRect = new Rect(MapPadding, MapPadding, renderW, renderH);
        var bitmap = GetBackgroundBitmap();
        if (bitmap is not null)
        {
            using (context.PushOpacity(MapBackgroundOpacity))
            {
                context.DrawImage(
                    bitmap,
                    new Rect(0, 0, bitmap.PixelSize.Width, bitmap.PixelSize.Height),
                    mapRect);
            }
        }

        context.DrawRectangle(MapVeilBrush, null, mapRect);
    }

    private Bitmap? GetBackgroundBitmap()
    {
        var mapKey = CurrentMap?.Trim();
        if (string.Equals(_backgroundMapKey, mapKey, StringComparison.Ordinal))
            return _backgroundBitmap;

        _backgroundMapKey = mapKey;
        _backgroundBitmap = TacticalMapAssets.TryGetBackground(mapKey);
        return _backgroundBitmap;
    }

    private void RenderEntitiesByType(DrawingContext context, int type,
        double sx, double sy, double renderH)
    {
        foreach (var (id, m) in _entities)
        {
            if (m.Type != type) continue;
            var pos = ToScreen(m.DisplayX, m.DisplayY, sx, sy, renderH);

            switch (type)
            {
                case 5: // station
                    context.DrawEllipse(StationBrush, null, pos, 7, 5);
                    context.DrawEllipse(null, StationBorderPen, pos, 7, 5);
                    DrawName(context, id, m.Name, pos, NameStationBrush, 10);
                    break;
                case 4: // portal
                    context.DrawEllipse(null, PortalRingPen, pos, 10, 10);
                    context.DrawRectangle(PortalBrush, null, new Rect(pos.X - 1.5, pos.Y - 1.5, 3, 3));
                    DrawName(context, id, m.Name, pos, NamePortalBrush, 13);
                    break;
                case 3: // box
                    context.DrawRectangle(BoxBrush, null, new Rect(pos.X - 2, pos.Y - 2, 4, 4));
                    break;
                case 0: // npc
                    context.DrawRectangle(NpcBrush, null, new Rect(pos.X - 2.5, pos.Y - 2.5, 5, 5));
                    DrawName(context, id, m.Name, pos, NameNpcBrush, 5);
                    break;
                case 1: // enemy
                    context.DrawRectangle(EnemyBrush, null, new Rect(pos.X - 2.5, pos.Y - 2.5, 5, 5));
                    DrawName(context, id, m.Name, pos, NameEnemyBrush, 5);
                    break;
                case 2: // ally
                    context.DrawRectangle(AllyBrush, null, new Rect(pos.X - 2.5, pos.Y - 2.5, 5, 5));
                    DrawName(context, id, m.Name, pos, NameAllyBrush, 5);
                    break;
            }
        }
    }

    private static Point ToScreen(double gameX, double gameY, double sx, double sy, double renderH)
    {
        return new Point(MapPadding + gameX * sx, MapPadding + renderH - gameY * sy);
    }

    private static double DistanceSquared(double ax, double ay, double bx, double by)
    {
        var dx = ax - bx;
        var dy = ay - by;
        return dx * dx + dy * dy;
    }

    private void DrawName(DrawingContext context, int entityId, string name, Point pos,
        IBrush brush, double offsetX)
    {
        if (string.IsNullOrEmpty(name)) return;

        if (!_textCache.TryGetValue(entityId, out var cached))
        {
            var text = new FormattedText(name, CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, NameTypeface, 8.5, brush);
            var shadow = new FormattedText(name, CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, NameTypeface, 8.5, NameShadowBrush);
            cached = (text, shadow);
            _textCache[entityId] = cached;
        }

        context.DrawText(cached.shadow, new Point(pos.X + offsetX + 1, pos.Y - 8));
        context.DrawText(cached.text, new Point(pos.X + offsetX, pos.Y - 9));
    }
}
