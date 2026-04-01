using System;
using System.Collections.Generic;
using System.Globalization;
using Avalonia.Data.Converters;

namespace DynamoGUI.Controls;

public sealed class PercentWidthConverter : IMultiValueConverter
{
    public static readonly PercentWidthConverter Instance = new();

    public object? Convert(IList<object?> values, Type targetType, object? parameter, CultureInfo culture)
    {
        if (values.Count < 2)
            return 0.0;

        var percent = values[0] switch
        {
            int i => i,
            double d => d,
            _ => 0.0
        };

        var maxWidth = values[1] switch
        {
            int i => (double)i,
            double d => d,
            string s when double.TryParse(s, CultureInfo.InvariantCulture, out var v) => v,
            _ => 80.0
        };

        return Math.Max(0, Math.Min(maxWidth, maxWidth * Math.Clamp(percent, 0, 100) / 100.0));
    }
}
