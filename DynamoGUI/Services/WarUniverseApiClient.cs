using DynamoGUI.Models;
using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace DynamoGUI.Services;

public static class WarUniverseApiClient
{
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(8) };
    private const string MetaInfoUrl = "https://eu.api.waruniverse.space/meta-info";

    public static async Task<List<ServerEntry>> FetchServersAsync()
    {
        try
        {
            var json = await Http.GetFromJsonAsync<MetaInfoResponse>(MetaInfoUrl, JsonOpts);
            if (json?.GameServers is { Count: > 0 })
            {
                var result = new List<ServerEntry>();
                foreach (var gs in json.GameServers)
                {
                    if (string.IsNullOrWhiteSpace(gs.Id)) continue;
                    result.Add(new ServerEntry
                    {
                        Id = gs.Id,
                        Name = gs.Name ?? gs.Id,
                    });
                }
                if (result.Count > 0) return result;
            }
        }
        catch
        {
            // Fall through to defaults
        }

        return FallbackServers();
    }

    private static List<ServerEntry> FallbackServers() =>
    [
        new() { Id = "eu1", Name = "Europe 1" },
        new() { Id = "us1", Name = "US 1" },
    ];

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    private sealed class MetaInfoResponse
    {
        public List<GameServerInfo> GameServers { get; set; } = [];
    }

    private sealed class GameServerInfo
    {
        public string Id { get; set; } = string.Empty;
        public string? Name { get; set; }
        public string Host { get; set; } = string.Empty;
        public int Port { get; set; }
    }
}
