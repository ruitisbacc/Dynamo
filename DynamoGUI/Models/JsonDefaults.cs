using System;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace DynamoGUI.Models;

public static class JsonDefaults
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        Converters =
        {
            new JsonStringEnumConverter()
        }
    };

    public static T DeepClone<T>(T value)
    {
        if (value is null)
        {
            throw new ArgumentNullException(nameof(value));
        }

        var json = JsonSerializer.Serialize(value, Options);
        return JsonSerializer.Deserialize<T>(json, Options)
            ?? throw new InvalidOperationException("Failed to clone serialized value.");
    }

    public static string Serialize<T>(T value) => JsonSerializer.Serialize(value, Options);
}
