using System.Globalization;
using System.Text.Json;

namespace CJPowerReverse.Protocol;

/// <summary>Loads the array-of-arrays format used by conReadWrite.dts.</summary>
public static class DtsCatalog
{
    public static IReadOnlyList<DtsParameter> Load(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        using FileStream stream = File.OpenRead(path);
        using JsonDocument document = JsonDocument.Parse(stream);

        if (document.RootElement.ValueKind != JsonValueKind.Array)
        {
            throw new FormatException("The DTS root must be an array of parameter groups.");
        }

        List<DtsParameter> parameters = [];
        int groupIndex = 0;
        foreach (JsonElement group in document.RootElement.EnumerateArray())
        {
            if (group.ValueKind != JsonValueKind.Array)
            {
                throw new FormatException($"DTS group {groupIndex} is not an array.");
            }

            foreach (JsonElement item in group.EnumerateArray())
            {
                string title = RequiredText(item, "title");
                ushort address = checked((ushort)RequiredInteger(item, "adress"));
                ParameterDataType type = ParameterValueCodec.Parse(RequiredText(item, "adrSize"));
                double ratioK = OptionalDouble(item, "ratio_K", 1);
                double ratioB = OptionalDouble(item, "ratio_B", 0);
                bool disabled = item.TryGetProperty("disable", out JsonElement disabledElement) &&
                    disabledElement.ValueKind == JsonValueKind.True;
                List<string> options = [];
                if (item.TryGetProperty("data", out JsonElement data) && data.ValueKind == JsonValueKind.Array)
                {
                    foreach (JsonElement option in data.EnumerateArray())
                    {
                        options.Add(option.ToString());
                    }
                }

                parameters.Add(new DtsParameter(
                    groupIndex, title, address, type, ratioK, ratioB, disabled, options));
            }

            groupIndex++;
        }

        return parameters;
    }

    private static string RequiredText(JsonElement item, string name)
    {
        if (!item.TryGetProperty(name, out JsonElement element))
        {
            throw new FormatException($"A DTS item is missing '{name}'.");
        }

        string? value = element.ValueKind == JsonValueKind.String
            ? element.GetString()
            : element.ToString();
        return string.IsNullOrWhiteSpace(value)
            ? throw new FormatException($"A DTS item has an empty '{name}'.")
            : value.Trim();
    }

    private static long RequiredInteger(JsonElement item, string name)
    {
        string value = RequiredText(item, name);
        return long.Parse(value, NumberStyles.Integer, CultureInfo.InvariantCulture);
    }

    private static double OptionalDouble(JsonElement item, string name, double defaultValue)
    {
        if (!item.TryGetProperty(name, out JsonElement element))
        {
            return defaultValue;
        }

        string value = element.ValueKind == JsonValueKind.String
            ? element.GetString() ?? string.Empty
            : element.ToString();
        return double.Parse(value, NumberStyles.Float, CultureInfo.InvariantCulture);
    }
}
