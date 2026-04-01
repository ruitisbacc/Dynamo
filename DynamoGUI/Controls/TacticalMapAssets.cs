using System;
using System.Collections.Generic;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace DynamoGUI.Controls;

internal static class TacticalMapAssets
{
    private static readonly Dictionary<string, Bitmap?> Cache = new(StringComparer.OrdinalIgnoreCase);
    private static readonly string AssemblyName =
        typeof(TacticalMapAssets).Assembly.GetName().Name ?? "Dynamo";

    public static Bitmap? TryGetBackground(string? mapId)
    {
        var normalized = NormalizeMapId(mapId);
        if (string.IsNullOrEmpty(normalized))
            return null;

        if (Cache.TryGetValue(normalized, out var cached))
            return cached;

        Bitmap? bitmap;
        try
        {
            var uri = new Uri($"avares://{AssemblyName}/Assets/Maps/{normalized.ToLowerInvariant()}.png");
            using var stream = AssetLoader.Open(uri);
            bitmap = new Bitmap(stream);
        }
        catch
        {
            bitmap = null;
        }

        Cache[normalized] = bitmap;
        return bitmap;
    }

    private static string NormalizeMapId(string? mapId)
    {
        if (string.IsNullOrWhiteSpace(mapId))
            return string.Empty;

        return mapId.Trim().ToUpperInvariant();
    }
}
