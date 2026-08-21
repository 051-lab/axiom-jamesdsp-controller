using System.Globalization;
using System.Text.RegularExpressions;

namespace AxiomJamesDSPController;

public sealed record LiveProgParameterMetadata(
    string Key,
    string Description,
    double? Default,
    double? InitialValue,
    double Minimum,
    double Maximum,
    double Step,
    IReadOnlyList<string> Options);

public sealed record LiveProgMetadata(
    string? Description,
    IReadOnlyList<string> Tags,
    IReadOnlyList<LiveProgParameterMetadata> Parameters);

public static partial class LiveProgMetadataParser
{
    public const int MaximumParameters = 128;

    private const string NumberPattern = @"[+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?";

    [GeneratedRegex(@"^\s*(?://\s*)?(?<key>[A-Za-z_][A-Za-z0-9_]*)\s*:\s*(?<default>[+-]?\d+)?\s*<\s*(?<min>[+-]?\d+)\s*,\s*(?<max>[+-]?\d+)(?:\s*,\s*(?<step>[+-]?\d+))?\s*\{(?<options>[^}]*)\}\s*>\s*(?<description>.*)\s*$")]
    private static partial Regex ListDeclaration();

    private static readonly Regex RangeDeclaration = new(
        $@"^\s*(?://\s*)?(?<key>[A-Za-z_][A-Za-z0-9_]*)\s*:\s*(?<default>{NumberPattern})?\s*<\s*(?<min>{NumberPattern})\s*,\s*(?<max>{NumberPattern})(?:\s*,\s*(?<step>{NumberPattern}))?\s*>\s*(?<description>.*)\s*$",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);

    public static LiveProgMetadata Parse(string source)
    {
        ArgumentNullException.ThrowIfNull(source);

        string? description = null;
        var tags = Array.Empty<string>();
        var parameters = new List<LiveProgParameterMetadata>();
        var keys = new HashSet<string>(StringComparer.Ordinal);

        using var reader = new StringReader(source);
        while (reader.ReadLine() is { } line)
        {
            if (line.TrimStart().StartsWith('@')) break;

            if (description is null && line.StartsWith("desc:", StringComparison.Ordinal))
            {
                description = line[5..].Trim();
                continue;
            }

            var tagIndex = line.IndexOf("tags:", StringComparison.Ordinal);
            if (tags.Length == 0 && tagIndex >= 0 && line[..tagIndex].All(character =>
                    char.IsWhiteSpace(character) || character == '/'))
            {
                tags = line[(tagIndex + 5)..]
                    .Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
                continue;
            }

            var parameter = ParseList(line) ?? ParseRange(line);
            if (parameter is null || !keys.Add(parameter.Key)) continue;
            parameters.Add(parameter);
            if (parameters.Count == MaximumParameters) break;
        }

        return new LiveProgMetadata(
            description,
            tags,
            parameters.Select(parameter =>
            {
                var initialValue = parameter.Default ?? FindInitialValue(source, parameter.Key);
                return parameter with
                {
                    InitialValue = initialValue is null
                        ? null
                        : Math.Clamp(initialValue.Value, parameter.Minimum, parameter.Maximum)
                };
            }).ToArray());
    }

    private static LiveProgParameterMetadata? ParseList(string line)
    {
        var match = ListDeclaration().Match(line);
        if (!match.Success ||
            !TryParseInteger(match.Groups["min"].Value, out var minimum) ||
            !TryParseInteger(match.Groups["max"].Value, out var maximum) ||
            !TryParseInteger(match.Groups["step"].Success ? match.Groups["step"].Value : "1", out var step) ||
            minimum != 0 || minimum >= maximum || step != 1)
        {
            return null;
        }

        double? defaultValue = null;
        if (match.Groups["default"].Success)
        {
            if (!TryParseInteger(match.Groups["default"].Value, out var parsedDefault)) return null;
            if (parsedDefault < minimum || parsedDefault > maximum) return null;
            defaultValue = parsedDefault;
        }

        var options = match.Groups["options"].Value
            .Split(',', StringSplitOptions.TrimEntries);
        if (options.Length == 0 || options.Any(string.IsNullOrEmpty) || maximum != options.Length - 1) return null;

        return new LiveProgParameterMetadata(
            match.Groups["key"].Value,
            match.Groups["description"].Value.Trim(),
            defaultValue,
            null,
            minimum,
            maximum,
            step,
            options);
    }

    private static LiveProgParameterMetadata? ParseRange(string line)
    {
        var match = RangeDeclaration.Match(line);
        if (!match.Success ||
            !TryParseFinite(match.Groups["min"].Value, out var minimum) ||
            !TryParseFinite(match.Groups["max"].Value, out var maximum) ||
            !TryParseFinite(match.Groups["step"].Success ? match.Groups["step"].Value : "0.1", out var step) ||
            minimum >= maximum || step <= 0)
        {
            return null;
        }

        double? defaultValue = null;
        if (match.Groups["default"].Success)
        {
            if (!TryParseFinite(match.Groups["default"].Value, out var parsedDefault)) return null;
            if (parsedDefault < minimum || parsedDefault > maximum) return null;
            defaultValue = parsedDefault;
        }

        return new LiveProgParameterMetadata(
            match.Groups["key"].Value,
            match.Groups["description"].Value.Trim(),
            defaultValue,
            null,
            minimum,
            maximum,
            step,
            Array.Empty<string>());
    }

    private static double? FindInitialValue(string source, string key)
    {
        var assignment = new Regex(
            $@"(?<![\w.]){Regex.Escape(key)}\s*=\s*(?<value>{NumberPattern})\s*;",
            RegexOptions.CultureInvariant);
        using var reader = new StringReader(source);
        while (reader.ReadLine() is { } line)
        {
            var comment = line.IndexOf("//", StringComparison.Ordinal);
            var code = comment < 0 ? line : line[..comment];
            var match = assignment.Match(code);
            if (match.Success && TryParseFinite(match.Groups["value"].Value, out var value)) return value;
        }
        return null;
    }

    private static bool TryParseInteger(string text, out double value)
    {
        var parsed = int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var integer);
        value = integer;
        return parsed;
    }

    private static bool TryParseFinite(string text, out double value)
    {
        return double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value) &&
               double.IsFinite(value);
    }
}
