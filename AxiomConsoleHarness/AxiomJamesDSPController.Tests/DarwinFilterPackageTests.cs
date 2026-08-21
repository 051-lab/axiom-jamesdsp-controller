using System.IO.Compression;
using System.Text;
using AxiomJamesDSPController;
using Xunit;

namespace AxiomJamesDSPController.Tests;

public sealed class DarwinFilterPackageTests
{
    [Fact]
    public void ListsReadsAndExportsDarwinFilter()
    {
        var directory = Path.Combine(Path.GetTempPath(), "darwin-package-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        var package = Path.Combine(directory, "filters.zip");
        var wave = Path.Combine(directory, "selected.wav");
        try
        {
            using (var archive = ZipFile.Open(package, ZipArchiveMode.Create))
            {
                using (var manifest = new StreamWriter(archive.CreateEntry("filter.json").Open(), Encoding.UTF8))
                {
                    manifest.Write("""{"list":[{"title":"Reference","path":"/mnt/sdcard/filter/reference.flt"}]}""");
                }
                using var filter = new BinaryWriter(archive.CreateEntry("reference.flt").Open());
                for (var index = 0; index < 256; index++) filter.Write(index + 1);
            }

            var listed = Assert.Single(DarwinFilterPackage.List(package));
            Assert.Equal("Reference", listed.Title);
            var impulse = DarwinFilterPackage.Read(package, listed.FileName);
            Assert.Equal(256, impulse.Samples.Length);
            Assert.Equal(1f, impulse.Samples.Sum(), 0.0001f);
            Assert.NotEqual(0u, impulse.Crc);

            DarwinFilterPackage.WriteFloatWave(wave, impulse.Samples);
            var bytes = File.ReadAllBytes(wave);
            Assert.Equal("RIFF", Encoding.ASCII.GetString(bytes, 0, 4));
            Assert.Equal(44 + 256 * sizeof(float), bytes.Length);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void RejectsUnsafeManifestPath()
    {
        var package = Path.Combine(Path.GetTempPath(), "darwin-unsafe-" + Guid.NewGuid().ToString("N") + ".zip");
        try
        {
            using (var archive = ZipFile.Open(package, ZipArchiveMode.Create))
            {
                using var manifest = new StreamWriter(archive.CreateEntry("filter.json").Open(), Encoding.UTF8);
                manifest.Write("""{"list":[{"title":"Unsafe","path":"../unsafe.flt"}]}""");
            }
            Assert.Throws<InvalidDataException>(() => DarwinFilterPackage.List(package));
        }
        finally
        {
            File.Delete(package);
        }
    }

    [Fact]
    public void RejectsDuplicateFilterEntries()
    {
        var package = Path.Combine(Path.GetTempPath(), "darwin-duplicate-" + Guid.NewGuid().ToString("N") + ".zip");
        try
        {
            using (var archive = ZipFile.Open(package, ZipArchiveMode.Create))
            {
                using (var manifest = new StreamWriter(archive.CreateEntry("filter.json").Open(), Encoding.UTF8))
                {
                    manifest.Write("""{"list":[{"title":"Duplicate","path":"duplicate.flt"}]}""");
                }
                for (var copy = 0; copy < 2; copy++)
                {
                    using var filter = new BinaryWriter(archive.CreateEntry("duplicate.flt").Open());
                    for (var index = 0; index < 256; index++) filter.Write(index + 1);
                }
            }
            Assert.Throws<InvalidDataException>(() => DarwinFilterPackage.List(package));
        }
        finally
        {
            File.Delete(package);
        }
    }
}
