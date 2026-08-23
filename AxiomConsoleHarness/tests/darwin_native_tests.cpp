#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "DspConfig.h"

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename T>
void writeValue(std::ofstream& stream, const T& value)
{
    stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void writeIdentityImpulse(const std::filesystem::path& path)
{
    constexpr std::uint32_t sampleRate = 48000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bitsPerSample = 32;
    constexpr std::uint32_t sampleCount = 256;
    constexpr std::uint32_t dataBytes = sampleCount * sizeof(float);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write("RIFF", 4);
    const std::uint32_t riffBytes = 36 + dataBytes;
    writeValue(stream, riffBytes);
    stream.write("WAVEfmt ", 8);
    const std::uint32_t formatBytes = 16;
    const std::uint16_t floatFormat = 3;
    const std::uint32_t byteRate = sampleRate * channels * sizeof(float);
    const std::uint16_t blockAlign = channels * sizeof(float);
    writeValue(stream, formatBytes);
    writeValue(stream, floatFormat);
    writeValue(stream, channels);
    writeValue(stream, sampleRate);
    writeValue(stream, byteRate);
    writeValue(stream, blockAlign);
    writeValue(stream, bitsPerSample);
    stream.write("data", 4);
    writeValue(stream, dataBytes);
    const float one = 1.0f;
    const float zero = 0.0f;
    writeValue(stream, one);
    for (std::uint32_t index = 1; index < sampleCount; ++index) writeValue(stream, zero);
}

bool allFinite(const std::vector<float>& samples)
{
    for (float sample : samples) {
        if (!std::isfinite(sample)) return false;
    }
    return true;
}

void testDarwinConfig(const std::filesystem::path& directory)
{
    const auto path = directory / "darwin.ini";
    {
        std::ofstream file(path);
        file << "[Darwin]\n"
             << "enabled = true\n"
             << "impulseFile = selected.wav\n"
             << "harmonic = 17.5\n"
             << "autoHeadroom = false\n"
             << "[LiveProg]\n"
             << "param.MixedCase = 2.5\n";
    }

    DspConfig config;
    expect(ConfigFile::load(path.string(), config), "Darwin config loads");
    expect(config.darwinEnabled, "Darwin enabled value parses");
    expect(config.darwinImpulseFile == "selected.wav", "Darwin impulse path parses");
    expect(config.darwinHarmonic == 17.5, "Darwin harmonic value parses");
    expect(!config.darwinAutoHeadroom, "Darwin headroom value parses");
    expect(config.liveprogParams.count("MixedCase") == 1, "LiveProg parameter case is preserved");
}

void testDarwinLifecycle(const std::filesystem::path& directory)
{
    const auto impulsePath = directory / "identity.wav";
    writeIdentityImpulse(impulsePath);

    JamesDSPGlobalMemoryAllocation();
    auto dsp = std::make_unique<JamesDSPLib>();
    std::memset(dsp.get(), 0, sizeof(JamesDSPLib));
    JamesDSPInit(dsp.get(), 64, 48000.0f);

    {
        DspController controller(dsp.get(), 48000, 64);
        DspConfig config;
        config.bassBoostEnabled = false;
        config.stereoWideEnabled = false;
        config.darwinEnabled = true;
        config.darwinImpulseFile = impulsePath.string();
        config.darwinHarmonic = 12.0;
        config.darwinAutoHeadroom = true;
        controller.applyConfig(config, true);

        std::vector<float> block(64 * 2, 0.1f);
        for (int index = 0; index < 10; ++index) controller.process(block.data(), 64);
        expect(allFinite(block), "enabled Darwin path produces finite samples");

        config.darwinHarmonic = 28.0;
        controller.applyConfig(config);
        for (int index = 0; index < 10; ++index) controller.process(block.data(), 64);
        expect(allFinite(block), "Darwin replacement crossfade produces finite samples");

        config.darwinImpulseFile = (directory / "missing.wav").string();
        controller.applyConfig(config);
        controller.process(block.data(), 64);
        expect(allFinite(block), "invalid Darwin replacement preserves a working path");

        config.darwinEnabled = false;
        controller.applyConfig(config);
        for (int index = 0; index < 10; ++index) controller.process(block.data(), 64);
        expect(allFinite(block), "Darwin disable handoff produces finite samples");
    }

    JamesDSPFree(dsp.get());
    JamesDSPGlobalMemoryDeallocation();
}

} // namespace

int main()
{
    const auto directory = std::filesystem::temp_directory_path()
        / ("jamesdsp-darwin-native-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    try {
        testDarwinConfig(directory);
        testDarwinLifecycle(directory);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
        ++failures;
    }
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);

    if (failures) {
        std::cerr << failures << " Darwin native test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All native Darwin integration tests passed.\n";
    return EXIT_SUCCESS;
}
