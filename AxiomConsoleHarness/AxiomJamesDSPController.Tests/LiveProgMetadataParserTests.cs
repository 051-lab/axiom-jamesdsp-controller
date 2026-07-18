using AxiomJamesDSPController;
using Xunit;

namespace AxiomJamesDSPController.Tests;

public sealed class LiveProgMetadataParserTests
{
    [Fact]
    public void ParsesDescriptionTagsLegacyDeclarationsAndNumericFormats()
    {
        var metadata = LiveProgMetadataParser.Parse("""
            desc: Metadata parity
            // tags: axiom clean test
            // leading: .5 < -1, 1. > Leading decimal
            exponent: -2.5e-1<-1e0, 1E+0, 5e-2>Scientific notation
            // mode:1<0,2{Off, Normal, Wide}>Mode
            @init
            leading = .5;
            """);

        Assert.Equal("Metadata parity", metadata.Description);
        Assert.Equal(new[] { "axiom", "clean", "test" }, metadata.Tags);
        Assert.Equal(new[] { "leading", "exponent", "mode" }, metadata.Parameters.Select(item => item.Key));
        Assert.Equal(0.5, metadata.Parameters[0].Default);
        Assert.Equal(0.05, metadata.Parameters[1].Step);
        Assert.Equal(new[] { "Off", "Normal", "Wide" }, metadata.Parameters[2].Options);
    }

    [Fact]
    public void KeepsFirstDuplicateAndStopsAtCodeSections()
    {
        var metadata = LiveProgMetadataParser.Parse("""
            duplicate:1<0,2>First
            // duplicate:1<0,2>Second
            @init
            hidden:1<0,2>Not metadata
            """);

        var parameter = Assert.Single(metadata.Parameters);
        Assert.Equal("First", parameter.Description);
    }

    [Fact]
    public void LimitsDiscoveredParameters()
    {
        var declarations = string.Join('\n', Enumerable.Range(0, 130)
            .Select(index => $"p{index}:0<0,1>Parameter {index}"));

        var metadata = LiveProgMetadataParser.Parse(declarations + "\n@sample\nspl0 = spl0;");

        Assert.Equal(128, metadata.Parameters.Count);
        Assert.DoesNotContain(metadata.Parameters, item => item.Key == "p128");
    }

    [Fact]
    public void RejectsNonFiniteAndInvalidMetadata()
    {
        var metadata = LiveProgMetadataParser.Parse("""
            nonfinite:1e999<0,1>Invalid
            reverse:0<1,0>Invalid range
            zeroStep:0<0,1,0>Invalid step
            @sample
            spl0 = spl0;
            """);

        Assert.Empty(metadata.Parameters);
    }

    [Fact]
    public void PreservesDeclaredDefaultsWithoutInitAssignments()
    {
        var metadata = LiveProgMetadataParser.Parse("""
            slider1:.5<0,1,.1>Gain
            mode:2<0,2,1{Off, Normal, Wide}>Mode
            @slider
            gain = slider1;
            @sample
            spl0 *= gain;
            """);

        Assert.Equal(0.5, metadata.Parameters[0].Default);
        Assert.Equal(2, metadata.Parameters[1].Default);
    }
}
