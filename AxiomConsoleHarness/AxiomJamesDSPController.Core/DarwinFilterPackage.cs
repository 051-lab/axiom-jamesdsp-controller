using System.Buffers.Binary;
using System.IO.Compression;
using System.Text.Json;

namespace AxiomJamesDSPController;

public sealed record DarwinFilter(string Title, string FileName);
public sealed record DarwinImpulse(float[] Samples, uint Crc);

public static class DarwinFilterPackage
{
    private const int FilterTaps = 256;
    private const int FilterBytes = FilterTaps * sizeof(int);
    private const int MaximumManifestBytes = 64 * 1024;
    private const int MaximumFilters = 256;
    private const string HibyFilterPrefix = "/mnt/sdcard/filter/";

    public static IReadOnlyList<DarwinFilter> List(string path)
    {
        using var archive = ZipFile.OpenRead(path);
        return List(archive);
    }

    public static DarwinImpulse Read(string path, string selectedFile)
    {
        using var archive = ZipFile.OpenRead(path);
        var filters = List(archive);
        var filter = filters.FirstOrDefault(item => item.FileName.Equals(selectedFile, StringComparison.Ordinal))
            ?? filters[0];
        var entry = archive.GetEntry(filter.FileName)
            ?? throw new InvalidDataException($"Missing {filter.FileName}.");
        var bytes = ReadExact(entry, FilterBytes);
        var coefficients = new double[FilterTaps];
        var sum = 0d;
        var compensation = 0d;
        var magnitude = 0d;
        for (var index = 0; index < coefficients.Length; index++)
        {
            var value = BinaryPrimitives.ReadInt32LittleEndian(bytes.AsSpan(index * sizeof(int), sizeof(int))) / 2147483648d;
            coefficients[index] = value;
            var corrected = value - compensation;
            var next = sum + corrected;
            compensation = (next - sum) - corrected;
            sum = next;
            magnitude += Math.Abs(value);
        }
        if (!double.IsFinite(sum) || !double.IsFinite(magnitude) || magnitude <= 0 || Math.Abs(sum) < magnitude * 0.01)
        {
            throw new InvalidDataException("Darwin filter coefficients cannot be normalized safely.");
        }

        var samples = coefficients.Select(value => (float)(value / sum)).ToArray();
        if (samples.Any(value => !float.IsFinite(value)))
        {
            throw new InvalidDataException("Darwin filter contains non-finite coefficients.");
        }
        return new DarwinImpulse(samples, Crc32(bytes));
    }

    public static void WriteFloatWave(string path, IReadOnlyList<float> samples, int sampleRate = 48_000)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(sampleRate);
        if (samples.Count == 0 || samples.Any(value => !float.IsFinite(value)))
        {
            throw new ArgumentException("Impulse samples must be finite and non-empty.", nameof(samples));
        }

        using var stream = File.Create(path);
        using var writer = new BinaryWriter(stream);
        var dataBytes = checked(samples.Count * sizeof(float));
        writer.Write("RIFF"u8);
        writer.Write(36 + dataBytes);
        writer.Write("WAVE"u8);
        writer.Write("fmt "u8);
        writer.Write(16);
        writer.Write((short)3);
        writer.Write((short)1);
        writer.Write(sampleRate);
        writer.Write(sampleRate * sizeof(float));
        writer.Write((short)sizeof(float));
        writer.Write((short)32);
        writer.Write("data"u8);
        writer.Write(dataBytes);
        foreach (var sample in samples) writer.Write(sample);
    }

    private static IReadOnlyList<DarwinFilter> List(ZipArchive archive)
    {
        var manifests = archive.Entries
            .Where(entry => entry.FullName.Equals("filter.json", StringComparison.Ordinal))
            .ToArray();
        if (manifests.Length != 1) throw new InvalidDataException("Darwin packages must contain exactly one filter.json.");
        var manifest = manifests[0];
        if (manifest.Length is <= 0 or > MaximumManifestBytes)
        {
            throw new InvalidDataException("Invalid Darwin filter manifest size.");
        }

        var manifestBytes = ReadExact(manifest, checked((int)manifest.Length));
        using var document = ParseManifest(manifestBytes);
        if (!document.RootElement.TryGetProperty("list", out var list) || list.ValueKind != JsonValueKind.Array)
        {
            throw new InvalidDataException("Missing Darwin filter list.");
        }

        var result = new List<DarwinFilter>();
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var item in list.EnumerateArray())
        {
            if (result.Count == MaximumFilters) throw new InvalidDataException("Darwin filter list is too large.");
            if (item.ValueKind != JsonValueKind.Object)
            {
                throw new InvalidDataException("Invalid Darwin filter manifest entry.");
            }
            var title = item.TryGetProperty("title", out var titleValue) && titleValue.ValueKind == JsonValueKind.String
                ? titleValue.GetString()?.Trim() ?? ""
                : "";
            var entryPath = item.TryGetProperty("path", out var pathValue) && pathValue.ValueKind == JsonValueKind.String
                ? pathValue.GetString() ?? ""
                : "";
            var fileName = entryPath[(entryPath.LastIndexOf('/') + 1)..];
            if (title.Length is 0 or > 128
                || !fileName.EndsWith(".flt", StringComparison.OrdinalIgnoreCase)
                || entryPath != fileName && entryPath != HibyFilterPrefix + fileName
                || entryPath.Contains('\\') || entryPath.Contains(':')
                || Path.GetFileName(fileName) != fileName
                || !names.Add(fileName))
            {
                throw new InvalidDataException("Invalid Darwin filter manifest entry.");
            }
            var entries = archive.Entries
                .Where(entry => entry.FullName.Equals(fileName, StringComparison.Ordinal))
                .ToArray();
            if (entries.Length != 1 || entries[0].Length != FilterBytes)
            {
                throw new InvalidDataException($"Missing or invalid {fileName}.");
            }
            result.Add(new DarwinFilter(title, fileName));
        }
        return result.Count > 0 ? result : throw new InvalidDataException("Darwin filter list is empty.");
    }

    private static JsonDocument ParseManifest(byte[] bytes)
    {
        var json = bytes.AsMemory();
        if (bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) json = json[3..];
        try
        {
            return JsonDocument.Parse(json);
        }
        catch (JsonException exc)
        {
            throw new InvalidDataException("Invalid Darwin filter manifest JSON.", exc);
        }
    }

    private static byte[] ReadExact(ZipArchiveEntry entry, int length)
    {
        if (entry.Length != length) throw new InvalidDataException($"Invalid size for {entry.FullName}.");
        var bytes = new byte[length];
        using var stream = entry.Open();
        stream.ReadExactly(bytes);
        if (stream.ReadByte() != -1) throw new InvalidDataException($"Oversized {entry.FullName}.");
        return bytes;
    }

    private static uint Crc32(IEnumerable<byte> bytes)
    {
        var crc = uint.MaxValue;
        foreach (var value in bytes)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (uint)-(int)(crc & 1));
        }
        return ~crc;
    }
}
